/* Copyright holders: Sarah Walker, leilei
   see COPYING for more details
*/
#include <stdlib.h>
#include <stddef.h>
#include "ibm.h"
#include "device.h"
#include "mem.h"
#include "pci.h"
#include "thread.h"
#include "timer.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_voodoo.h"
#include "vid_voodoo_dither.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define CLAMP(x) (((x) < 0) ? 0 : (((x) > 0xff) ? 0xff : (x)))
#define CLAMP16(x) (((x) < 0) ? 0 : (((x) > 0xffff) ? 0xffff : (x)))

#define LOD_MAX 8

static int tris = 0;

static uint64_t status_time = 0;
static uint64_t voodoo_time = 0;
static int voodoo_render_time[2] = {0, 0};
static int voodoo_render_time_old[2] = {0, 0};

typedef union int_float
{
        uint32_t i;
        float f;
} int_float;

typedef struct rgb_t
{
        uint8_t b, g, r;
        uint8_t pad;
} rgb_t;
typedef struct rgba8_t
{
        uint8_t b, g, r, a;
} rgba8_t;

typedef union rgba_u
{
        struct
        {
                uint8_t b, g, r, a;
        } rgba;
        uint32_t u;
} rgba_u;

#define FIFO_SIZE 65536
#define FIFO_MASK (FIFO_SIZE - 1)
#define FIFO_ENTRY_SIZE (1 << 31)

#define FIFO_ENTRIES (voodoo->fifo_write_idx - voodoo->fifo_read_idx)
#define FIFO_FULL    ((voodoo->fifo_write_idx - voodoo->fifo_read_idx) >= FIFO_SIZE)
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
        } tmu[1];

        uint32_t color0, color1;

        uint32_t fbzMode;
        uint32_t fbzColorPath;
        
        uint32_t fogMode;
        rgb_t fogColor;
        struct
        {
                uint8_t fog, dfog;
        } fogTable[64];

        uint32_t alphaMode;
        
        uint32_t zaColor;
        
        int chromaKey_r, chromaKey_g, chromaKey_b;
        uint32_t chromaKey;

        uint32_t textureMode;
        uint32_t tLOD;

        uint32_t texBaseAddr, texBaseAddr1, texBaseAddr2, texBaseAddr38;
        
        uint32_t tex_base[LOD_MAX+1];
        int tex_width;
        int tex_w_mask[LOD_MAX+1];
        int tex_w_nmask[LOD_MAX+1];
        int tex_h_mask[LOD_MAX+1];
        int tex_shift[LOD_MAX+1];
        
        uint32_t draw_offset, aux_offset;

        int tformat;
        
        int clipLeft, clipRight, clipLowY, clipHighY;
        
        int sign;
        
        uint32_t front_offset;
        
        uint32_t swapbufferCMD;

        rgba_u palette[256];
} voodoo_params_t;

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
        
        uint8_t initEnable;
        
        uint32_t lfbMode;
        
        uint32_t memBaseAddr;

        int_float fvertexAx, fvertexAy, fvertexBx, fvertexBy, fvertexCx, fvertexCy;

        uint32_t front_offset, back_offset;
        
        uint32_t fb_read_offset, fb_write_offset;
        
        int row_width;
        
        uint8_t *fb_mem, *tex_mem;
        uint16_t *tex_mem_w;
               
        int rgb_sel;
        
        uint32_t trexInit1;
        
        int swap_count;
        
        int disp_buffer;
        int64_t timer_count;
        
        int line;
        svga_t *svga;
        
        uint32_t backPorch;
        uint32_t videoDimensions;
        uint32_t hSync, vSync;
        
        int v_total, v_disp;
        int h_disp;
        int v_retrace;

        struct
        {
                uint32_t y[4], i[4], q[4];
        } nccTable[2];

        rgba_u palette[256];
        
        rgba_u ncc_lookup[2][256];
        int ncc_dirty;

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
        
        int pixel_count[2], tri_count, frame_count;
        int pixel_count_old[2];
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
        
        fifo_entry_t fifo[FIFO_SIZE];
        volatile int fifo_read_idx, fifo_write_idx;
        int cmd_read, cmd_written;

        voodoo_params_t params_buffer[PARAM_SIZE];
        volatile int params_read_idx[2], params_write_idx;
        
        int flush;

        int scrfilter;

        uint32_t last_write_addr;

        uint32_t fbiPixelsIn;
        uint32_t fbiChromaFail;
        uint32_t fbiZFuncFail;
        uint32_t fbiAFuncFail;
        uint32_t fbiPixelsOut;
                
        rgb_t clutData[33];
        int clutData_dirty;
        rgb_t clutData256[256];
        uint32_t video_16to32[0x10000];
        
        uint8_t dirty_line[1024];
        int dirty_line_low, dirty_line_high;
        
        int fb_write_buffer, fb_draw_buffer;

        uint16_t thefilter[1024][1024]; // pixel filter, feeding from one or two
        uint16_t thefilterg[1024][1024]; // for green

        /* the voodoo adds purple lines for some reason */
        uint16_t purpleline[1024];

        int use_recompiler;        
        void *codegen_data;
} voodoo_t;

enum
{
        SST_status = 0x000,
        
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
        
        SST_fbiInit4 = 0x200,

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

        SST_textureMode = 0x300,
        SST_tLOD = 0x304,
        
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
        TEXTUREMODE_TCLAMPT = (1 << 7)
};

enum
{
        FBIINIT0_VGA_PASS = 1
};

enum
{
        FBIINIT3_REMAP = 1
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
        CC_MSELECT_TEX    = 4
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
        FOG_CONSTANT = 0x20
};

enum
{
        LOD_S_IS_WIDER = (1 << 20)
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

static void voodoo_update_ncc(voodoo_t *voodoo)
{
        int tbl;
        
        for (tbl = 0; tbl < 2; tbl++)
        {
                int col;
                
                for (col = 0; col < 256; col++)
                {
                        int y = (col >> 4), i = (col >> 2) & 3, q = col & 3;
                        int _y = (col >> 4), _i = (col >> 2) & 3, _q = col & 3;
                        int i_r, i_g, i_b;
                        int q_r, q_g, q_b;
                        int r, g, b;
                        
                        y = (voodoo->nccTable[tbl].y[y >> 2] >> ((y & 3) * 8)) & 0xff;
                        
                        i_r = (voodoo->nccTable[tbl].i[i] >> 18) & 0x1ff;
                        if (i_r & 0x100)
                                i_r |= 0xfffffe00;
                        i_g = (voodoo->nccTable[tbl].i[i] >> 9) & 0x1ff;
                        if (i_g & 0x100)
                                i_g |= 0xfffffe00;
                        i_b = voodoo->nccTable[tbl].i[i] & 0x1ff;
                        if (i_b & 0x100)
                                i_b |= 0xfffffe00;

                        q_r = (voodoo->nccTable[tbl].q[q] >> 18) & 0x1ff;
                        if (q_r & 0x100)
                                q_r |= 0xfffffe00;
                        q_g = (voodoo->nccTable[tbl].q[q] >> 9) & 0x1ff;
                        if (q_g & 0x100)
                                q_g |= 0xfffffe00;
                        q_b = voodoo->nccTable[tbl].q[q] & 0x1ff;
                        if (q_b & 0x100)
                                q_b |= 0xfffffe00;
                        
                        voodoo->ncc_lookup[tbl][col].rgba.r = CLAMP(y + i_r + q_r);
                        voodoo->ncc_lookup[tbl][col].rgba.g = CLAMP(y + i_g + q_g);
                        voodoo->ncc_lookup[tbl][col].rgba.b = CLAMP(y + i_b + q_b);
                        voodoo->ncc_lookup[tbl][col].rgba.a = 0xff;
                }
        }
}

static void voodoo_recalc(voodoo_t *voodoo)
{
        uint32_t buffer_offset = ((voodoo->fbiInit2 >> 11) & 511) * 4096;
        
        if (voodoo->disp_buffer)
        {
                voodoo->back_offset = 0;
                voodoo->params.front_offset = buffer_offset;
        }
        else
        {
                voodoo->params.front_offset = 0;
                voodoo->back_offset = buffer_offset;
        }
        voodoo->params.aux_offset = buffer_offset * 2;
        
        switch (voodoo->lfbMode & LFB_WRITE_MASK)
        {
                case LFB_WRITE_FRONT:
                voodoo->fb_write_offset = voodoo->params.front_offset;
                voodoo->fb_write_buffer = voodoo->disp_buffer ? 1 : 0;
                break;
                case LFB_WRITE_BACK:
                voodoo->fb_write_offset = voodoo->back_offset;
                voodoo->fb_write_buffer = voodoo->disp_buffer ? 0 : 1;
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
                voodoo->fb_draw_buffer = voodoo->disp_buffer ? 1 : 0;
                break;
                case FBZ_DRAW_BACK:
                voodoo->params.draw_offset = voodoo->back_offset;
                voodoo->fb_draw_buffer = voodoo->disp_buffer ? 0 : 1;
                break;

                default:
                fatal("voodoo_recalc : unknown draw buffer\n");
        }
                
        voodoo->row_width = ((voodoo->fbiInit1 >> 4) & 15) * 64 * 2;
//        pclog("voodoo_recalc : front_offset %08X  back_offset %08X  aux_offset %08X draw_offset %08x\n", voodoo->params.front_offset, voodoo->back_offset, voodoo->params.aux_offset, voodoo->params.draw_offset);
//        pclog("                fb_read_offset %08X  fb_write_offset %08X  row_width %i  %08x %08x\n", voodoo->fb_read_offset, voodoo->fb_write_offset, voodoo->row_width, voodoo->lfbMode, voodoo->params.fbzMode);
}

static void voodoo_recalc_tex(voodoo_t *voodoo)
{
        int aspect = (voodoo->params.tLOD >> 21) & 3;
        int width = 256, height = 256;
        int shift = 8;
        int lod;
        int lod_min = (voodoo->params.tLOD >> 2) & 15;
        int lod_max = (voodoo->params.tLOD >> 8) & 15;
        uint32_t base = voodoo->params.texBaseAddr;
        
        if (voodoo->params.tLOD & LOD_S_IS_WIDER)
                height >>= aspect;
        else
        {
                width >>= aspect;
                shift -= aspect;
        }
        
        for (lod = 0; lod <= LOD_MAX; lod++)
        {
                if (!width)
                        width = 1;
                if (!height)
                        height = 1;
                if (shift < 0)
                        shift = 0;
                voodoo->params.tex_base[lod] = base;
                voodoo->params.tex_w_mask[lod] = width - 1;
                voodoo->params.tex_w_nmask[lod] = ~(width - 1);
                voodoo->params.tex_h_mask[lod] = height - 1;
                voodoo->params.tex_shift[lod] = shift;
//                pclog("LOD%i base=%08x %i-%i %i,%i wm=%02x hm=%02x sh=%i\n", lod, base, lod_min, lod_max, width, height, voodoo->params.tex_w_mask[lod], voodoo->params.tex_h_mask[lod], voodoo->params.tex_shift[lod]);
                
                if (voodoo->params.tformat & 8)
                        base += width * height * 2;
                else
                        base += width * height;

                width >>= 1;
                height >>= 1;
                shift--;
        }
        
        voodoo->params.tex_width = width;
}

typedef struct voodoo_state_t
{
        int xstart, xend, xdir;
        uint32_t base_r, base_g, base_b, base_a, base_z;
        struct
        {
                int64_t base_s, base_t, base_w;
                int lod;
        } tmu[1];
        int64_t base_w;
        int lod;
        int lod_min, lod_max;
        int dx1, dx2;
        int y, yend, ydir;
        int32_t dxAB, dxAC, dxBC;
        int tex_b, tex_g, tex_r, tex_a;
        int tex_s, tex_t;
        int clamp_s, clamp_t;

        int32_t vertexAx, vertexAy, vertexBx, vertexBy, vertexCx, vertexCy;
        
        uint8_t *tex[LOD_MAX+1];
        uint16_t *tex_w[LOD_MAX+1];
        int tformat;
        
        rgba_u *palette;

        int *tex_w_mask;
        int *tex_h_mask;
        int *tex_shift;

        uint16_t *fb_mem, *aux_mem;

        int32_t ib, ig, ir, ia;
        int32_t z;
        
        int32_t new_depth;

        int64_t tmu0_s, tmu0_t;
        int64_t tmu0_w;
        int64_t w;
        
        int pixel_count;
        int x, x2;
        
        uint32_t w_depth;
        
        float log_temp;
        uint32_t ebp_store;
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

static inline int fls(uint16_t val)
{
        int num = 0;
        
//pclog("fls(%04x) = ", val);
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
//pclog("%i %04x\n", num, val);
        return num;
}

typedef struct voodoo_texture_state_t
{
        int s, t;
        int w_mask, h_mask;
        int tex_shift;
} voodoo_texture_state_t;

static inline void tex_read(voodoo_state_t *state, voodoo_texture_state_t *texture_state)
{
        uint16_t dat;
        
        if (texture_state->s & ~texture_state->w_mask)
        {
                if (state->clamp_s)
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
                if (state->clamp_t)
                {
                        if (texture_state->t < 0)
                                texture_state->t = 0;
                        if (texture_state->t > texture_state->h_mask)
                                texture_state->t = texture_state->h_mask;
                }
                else
                        texture_state->t &= texture_state->h_mask;
        }

        if (state->tformat & 8)
                dat = state->tex_w[state->lod][texture_state->s + (texture_state->t << texture_state->tex_shift)];
        else
                dat = state->tex[state->lod][texture_state->s + (texture_state->t << texture_state->tex_shift)];

        switch (state->tformat)
        {
                case TEX_RGB332:
                state->tex_r = rgb332[dat].r;
                state->tex_g = rgb332[dat].g;
                state->tex_b = rgb332[dat].b;
                state->tex_a = 0xff;
                break;
                                                
                case TEX_Y4I2Q2:
                state->tex_r = state->palette[dat].rgba.r;
                state->tex_g = state->palette[dat].rgba.g;
                state->tex_b = state->palette[dat].rgba.b;
                state->tex_a = 0xff;
                break;
                                                        
                case TEX_A8:
                state->tex_r = state->tex_g = state->tex_b = state->tex_a = dat & 0xff;
                break;

                case TEX_I8:
                state->tex_r = state->tex_g = state->tex_b = dat & 0xff;
                state->tex_a = 0xff;
                break;

                case TEX_AI8:
                state->tex_r = state->tex_g = state->tex_b = (dat & 0x0f) | ((dat << 4) & 0xf0);
                state->tex_a = (dat & 0xf0) | ((dat >> 4) & 0x0f);
                break;
                                                
                case TEX_PAL8:
                state->tex_r = state->palette[dat].rgba.r;
                state->tex_g = state->palette[dat].rgba.g;
                state->tex_b = state->palette[dat].rgba.b;
                state->tex_a = 0xff;
                break;
                                                        
                case TEX_R5G6B5:
                state->tex_r = rgb565[dat].r;
                state->tex_g = rgb565[dat].g;
                state->tex_b = rgb565[dat].b;
                state->tex_a = 0xff;
                break;

                case TEX_ARGB1555:
                state->tex_r = argb1555[dat].r;
                state->tex_g = argb1555[dat].g;
                state->tex_b = argb1555[dat].b;
                state->tex_a = argb1555[dat].a;
                break;

                case TEX_ARGB4444:
                state->tex_r = argb4444[dat].r;
                state->tex_g = argb4444[dat].g;
                state->tex_b = argb4444[dat].b;
                state->tex_a = argb4444[dat].a;
                break;
                                                        
                case TEX_A8I8:
                state->tex_r = state->tex_g = state->tex_b = dat & 0xff;
                state->tex_a = dat >> 8;
                break;

                case TEX_APAL88:
                state->tex_r = state->palette[dat & 0xff].rgba.r;
                state->tex_g = state->palette[dat & 0xff].rgba.g;
                state->tex_b = state->palette[dat & 0xff].rgba.b;
                state->tex_a = dat >> 8;
                break;
                                                        
                default:
                fatal("Unknown texture format %i\n", state->tformat);
        }
}

#define LOW4(x)  ((x & 0x0f) | ((x & 0x0f) << 4))
#define HIGH4(x) ((x & 0xf0) | ((x & 0xf0) >> 4))

static inline void tex_read_4(voodoo_state_t *state, voodoo_texture_state_t *texture_state, int s, int t, int *d)
{
        uint16_t dat[4];

        if (((s | (s + 1)) & ~texture_state->w_mask) || ((t | (t + 1)) & ~texture_state->h_mask))
        {
                int c;
                for (c = 0; c < 4; c++)
                {
                        int _s = s + (c & 1);
                        int _t = t + ((c & 2) >> 1);
                
                        if (_s & ~texture_state->w_mask)
                        {
                                if (state->clamp_s)
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
                                if (state->clamp_t)
                                {
                                        if (_t < 0)
                                                _t = 0;
                                        if (_t > texture_state->h_mask)
                                                _t = texture_state->h_mask;
                                }
                                else
                                        _t &= texture_state->h_mask;
                        }
                        if (state->tformat & 8)
                                dat[c] = state->tex_w[state->lod][_s + (_t << texture_state->tex_shift)];
                        else
                                dat[c] = state->tex[state->lod][_s + (_t << texture_state->tex_shift)];
                }
        }
        else
        {
                if (state->tformat & 8)
                {
                        dat[0] = state->tex_w[state->lod][s +     (t << texture_state->tex_shift)];
                        dat[1] = state->tex_w[state->lod][s + 1 + (t << texture_state->tex_shift)];
                        dat[2] = state->tex_w[state->lod][s +     ((t + 1) << texture_state->tex_shift)];
                        dat[3] = state->tex_w[state->lod][s + 1 + ((t + 1) << texture_state->tex_shift)];
                }
                else
                {
                        dat[0] = state->tex[state->lod][s +     (t << texture_state->tex_shift)];
                        dat[1] = state->tex[state->lod][s + 1 + (t << texture_state->tex_shift)];
                        dat[2] = state->tex[state->lod][s +     ((t + 1) << texture_state->tex_shift)];
                        dat[3] = state->tex[state->lod][s + 1 + ((t + 1) << texture_state->tex_shift)];
                }
        }

        switch (state->tformat)
        {
                case TEX_RGB332:
                state->tex_r = (rgb332[dat[0]].r * d[0] + rgb332[dat[1]].r * d[1] + rgb332[dat[2]].r * d[2] + rgb332[dat[3]].r * d[3]) >> 8;
                state->tex_g = (rgb332[dat[0]].g * d[0] + rgb332[dat[1]].g * d[1] + rgb332[dat[2]].g * d[2] + rgb332[dat[3]].g * d[3]) >> 8;
                state->tex_b = (rgb332[dat[0]].b * d[0] + rgb332[dat[1]].b * d[1] + rgb332[dat[2]].b * d[2] + rgb332[dat[3]].b * d[3]) >> 8;
                state->tex_a = 0xff;
                break;
                                                
                case TEX_Y4I2Q2:
                state->tex_r = (state->palette[dat[0]].rgba.r * d[0] + state->palette[dat[1]].rgba.r * d[1] + state->palette[dat[2]].rgba.r * d[2] + state->palette[dat[3]].rgba.r * d[3]) >> 8;
                state->tex_g = (state->palette[dat[0]].rgba.g * d[0] + state->palette[dat[1]].rgba.g * d[1] + state->palette[dat[2]].rgba.g * d[2] + state->palette[dat[3]].rgba.g * d[3]) >> 8;
                state->tex_b = (state->palette[dat[0]].rgba.b * d[0] + state->palette[dat[1]].rgba.b * d[1] + state->palette[dat[2]].rgba.b * d[2] + state->palette[dat[3]].rgba.b * d[3]) >> 8;
                state->tex_a = 0xff;
                break;
                                                        
                case TEX_A8:
                state->tex_r = state->tex_g = state->tex_b = state->tex_a = (dat[0] * d[0] + dat[1] * d[1] + dat[2] * d[2] + dat[3] * d[3]) >> 8;
                break;

                case TEX_I8:
                state->tex_r = state->tex_g = state->tex_b = (dat[0] * d[0] + dat[1] * d[1] + dat[2] * d[2] + dat[3] * d[3]) >> 8;
                state->tex_a = 0xff;
                break;

                case TEX_AI8:
                state->tex_r = state->tex_g = state->tex_b = (LOW4(dat[0]) * d[0] + LOW4(dat[1]) * d[1] + LOW4(dat[2]) * d[2] + LOW4(dat[3]) * d[3]) >> 8;
                state->tex_a = (HIGH4(dat[0]) * d[0] + HIGH4(dat[1]) * d[1] + HIGH4(dat[2]) * d[2] + HIGH4(dat[3]) * d[3]) >> 8;
                break;
                                                
                case TEX_PAL8:
                state->tex_r = (state->palette[dat[0]].rgba.r * d[0] + state->palette[dat[1]].rgba.r * d[1] + state->palette[dat[2]].rgba.r * d[2] + state->palette[dat[3]].rgba.r * d[3]) >> 8;
                state->tex_g = (state->palette[dat[0]].rgba.g * d[0] + state->palette[dat[1]].rgba.g * d[1] + state->palette[dat[2]].rgba.g * d[2] + state->palette[dat[3]].rgba.g * d[3]) >> 8;
                state->tex_b = (state->palette[dat[0]].rgba.b * d[0] + state->palette[dat[1]].rgba.b * d[1] + state->palette[dat[2]].rgba.b * d[2] + state->palette[dat[3]].rgba.b * d[3]) >> 8;
                state->tex_a = 0xff;
                break;
                                                        
                case TEX_R5G6B5:
                state->tex_r = (rgb565[dat[0]].r * d[0] + rgb565[dat[1]].r * d[1] + rgb565[dat[2]].r * d[2] + rgb565[dat[3]].r * d[3]) >> 8;
                state->tex_g = (rgb565[dat[0]].g * d[0] + rgb565[dat[1]].g * d[1] + rgb565[dat[2]].g * d[2] + rgb565[dat[3]].g * d[3]) >> 8;
                state->tex_b = (rgb565[dat[0]].b * d[0] + rgb565[dat[1]].b * d[1] + rgb565[dat[2]].b * d[2] + rgb565[dat[3]].b * d[3]) >> 8;
                state->tex_a = 0xff;
                break;

                case TEX_ARGB1555:
                state->tex_r = (argb1555[dat[0]].r * d[0] + argb1555[dat[1]].r * d[1] + argb1555[dat[2]].r * d[2] + argb1555[dat[3]].r * d[3]) >> 8;
                state->tex_g = (argb1555[dat[0]].g * d[0] + argb1555[dat[1]].g * d[1] + argb1555[dat[2]].g * d[2] + argb1555[dat[3]].g * d[3]) >> 8;
                state->tex_b = (argb1555[dat[0]].b * d[0] + argb1555[dat[1]].b * d[1] + argb1555[dat[2]].b * d[2] + argb1555[dat[3]].b * d[3]) >> 8;
                state->tex_a = (argb1555[dat[0]].a * d[0] + argb1555[dat[1]].a * d[1] + argb1555[dat[2]].a * d[2] + argb1555[dat[3]].a * d[3]) >> 8;
                break;

                case TEX_ARGB4444:
                state->tex_r = (argb4444[dat[0]].r * d[0] + argb4444[dat[1]].r * d[1] + argb4444[dat[2]].r * d[2] + argb4444[dat[3]].r * d[3]) >> 8;
                state->tex_g = (argb4444[dat[0]].g * d[0] + argb4444[dat[1]].g * d[1] + argb4444[dat[2]].g * d[2] + argb4444[dat[3]].g * d[3]) >> 8;
                state->tex_b = (argb4444[dat[0]].b * d[0] + argb4444[dat[1]].b * d[1] + argb4444[dat[2]].b * d[2] + argb4444[dat[3]].b * d[3]) >> 8;
                state->tex_a = (argb4444[dat[0]].a * d[0] + argb4444[dat[1]].a * d[1] + argb4444[dat[2]].a * d[2] + argb4444[dat[3]].a * d[3]) >> 8;
                break;
                                                        
                case TEX_A8I8:
                state->tex_r = state->tex_g = state->tex_b = ((dat[0] & 0xff) * d[0] + (dat[1] & 0xff) * d[1] + (dat[2] & 0xff) * d[2] + (dat[3] & 0xff) * d[3]) >> 8;
                state->tex_a = ((dat[0] >> 8) * d[0] + (dat[1] >> 8) * d[1] + (dat[2] >> 8) * d[2] + (dat[3] >> 8) * d[3]) >> 8;
                break;

                case TEX_APAL88:
                state->tex_r = (state->palette[dat[0] & 0xff].rgba.r * d[0] + state->palette[dat[1] & 0xff].rgba.r * d[1] + state->palette[dat[2] & 0xff].rgba.r * d[2] + state->palette[dat[3] & 0xff].rgba.r * d[3]) >> 8;
                state->tex_g = (state->palette[dat[0] & 0xff].rgba.g * d[0] + state->palette[dat[1] & 0xff].rgba.g * d[1] + state->palette[dat[2] & 0xff].rgba.g * d[2] + state->palette[dat[3] & 0xff].rgba.g * d[3]) >> 8;
                state->tex_b = (state->palette[dat[0] & 0xff].rgba.b * d[0] + state->palette[dat[1] & 0xff].rgba.b * d[1] + state->palette[dat[2] & 0xff].rgba.b * d[2] + state->palette[dat[3] & 0xff].rgba.b * d[3]) >> 8;
                state->tex_a = ((dat[0] >> 8) * d[0] + (dat[1] >> 8) * d[1] + (dat[2] >> 8) * d[2] + (dat[3] >> 8) * d[3]) >> 8;
                break;
                                                        
                default:
                fatal("Unknown texture format %i\n", state->tformat);
        }
}

static inline void voodoo_get_texture(voodoo_t *voodoo, voodoo_params_t *params, voodoo_state_t *state)
{
        rgba_u tex_samples[4];
        voodoo_texture_state_t texture_state;
        int d[4];
        int s, t;

        texture_state.w_mask = state->tex_w_mask[state->lod];
        texture_state.h_mask = state->tex_h_mask[state->lod];
        texture_state.tex_shift = state->tex_shift[state->lod];
        
        if (voodoo->bilinear_enabled && params->textureMode & 6)
        {
                int _ds, dt;
                        
                state->tex_s -= 1 << (3+state->lod);
                state->tex_t -= 1 << (3+state->lod);
        
                s = state->tex_s >> state->lod;
                t = state->tex_t >> state->lod;

                _ds = s & 0xf;
                dt = t & 0xf;

                s >>= 4;
                t >>= 4;
                
//if (x == 80)
//if (voodoo_output)
//        pclog("s=%08x t=%08x _ds=%02x _dt=%02x\n", s, t, _ds, dt);
                d[0] = (16 - _ds) * (16 - dt);
                d[1] =  _ds * (16 - dt);
                d[2] = (16 - _ds) * dt;
                d[3] = _ds * dt;

//                texture_state.s = s;
//                texture_state.t = t;
                tex_read_4(state, &texture_state, s, t, d);

        
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
        
                s = state->tex_s >> (4+state->lod);
                t = state->tex_t >> (4+state->lod);

                texture_state.s = s;
                texture_state.t = t;
                tex_read(state, &texture_state);

/*                state->tex_r = tex_samples[0].rgba.r;
                state->tex_g = tex_samples[0].rgba.g;
                state->tex_b = tex_samples[0].rgba.b;
                state->tex_a = tex_samples[0].rgba.a;*/
        }
}


#define DEPTH_TEST()                                    \
        do                                              \
        {                                               \
                switch (depth_op)                       \
                {                                       \
                        case DEPTHOP_NEVER:             \
                        voodoo->fbiZFuncFail++;         \
                        goto skip_pixel;                \
                        case DEPTHOP_LESSTHAN:          \
                        if (!(new_depth < old_depth))   \
                        {                               \
                                voodoo->fbiZFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case DEPTHOP_EQUAL:             \
                        if (!(new_depth == old_depth))  \
                        {                               \
                                voodoo->fbiZFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case DEPTHOP_LESSTHANEQUAL:     \
                        if (!(new_depth <= old_depth))  \
                        {                               \
                                voodoo->fbiZFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case DEPTHOP_GREATERTHAN:       \
                        if (!(new_depth > old_depth))   \
                        {                               \
                                voodoo->fbiZFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case DEPTHOP_NOTEQUAL:          \
                        if (!(new_depth != old_depth))  \
                        {                               \
                                voodoo->fbiZFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case DEPTHOP_GREATERTHANEQUAL:  \
                        if (!(new_depth >= old_depth))  \
                        {                               \
                                voodoo->fbiZFuncFail++; \
                                goto skip_pixel;        \
                        }                               \
                        break;                          \
                        case DEPTHOP_ALWAYS:            \
                        break;                          \
                }                                       \
        } while (0)

#define APPLY_FOG(src_r, src_g, src_b, z, ia)                           \
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
                        int fog_r, fog_g, fog_b, fog_a;                 \
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
                        if (params->fogMode & FOG_Z)                    \
                                fog_a = (z >> 20) & 0xff;               \
                        else if (params->fogMode & FOG_ALPHA)           \
                                fog_a = CLAMP(ia >> 12);                \
                        else                                            \
                        {                                               \
                                int fog_idx = (w_depth >> 10) & 0x3f;   \
                                                                        \
                                fog_a = params->fogTable[fog_idx].fog;  \
                                fog_a += (params->fogTable[fog_idx].dfog * ((w_depth >> 2) & 0xff)) >> 10;      \
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
                int newdest_r, newdest_g, newdest_b;                    \
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
#define src_afunc ( (params->alphaMode >> 8) & 0xf)
#define dest_afunc ( (params->alphaMode >> 12) & 0xf)
#define alpha_func ( (params->alphaMode >> 1) & 7)
#define a_ref ( params->alphaMode >> 24)
#define depth_op ( (params->fbzMode >> 5) & 7)
#define dither ( params->fbzMode & FBZ_DITHER)
#define dither2x2 (params->fbzMode & FBZ_DITHER_2x2)

#if (defined i386 || defined __i386 || defined __i386__ || defined _X86_ || defined WIN32 || defined _WIN32 || defined _WIN32) && !(defined __amd64__)
#include "vid_voodoo_codegen_x86.h"
#elif (defined __amd64__)
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
        int c;
        uint8_t (*voodoo_draw)(voodoo_state_t *state, voodoo_params_t *params, int x, int real_y);
        
        state->clamp_s = params->textureMode & TEXTUREMODE_TCLAMPS;
        state->clamp_t = params->textureMode & TEXTUREMODE_TCLAMPT;
//        int last_x;
//        pclog("voodoo_triangle : bottom-half %X %X %X %X %X %i  %i %i %i\n", xstart, xend, dx1, dx2, dx2 * 36, xdir,  y, yend, ydir);

        for (c = 0; c <= LOD_MAX; c++)
        {
                state->tex[c] = &voodoo->tex_mem[params->tex_base[c] & voodoo->texture_mask];
                state->tex_w[c] = (uint16_t *)state->tex[c];
        }
        
        state->tformat = params->tformat;

        state->tex_w_mask = params->tex_w_mask;
        state->tex_h_mask = params->tex_h_mask;
        state->tex_shift = params->tex_shift;

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
                state->base_w += params->dWdY*dy;
                state->xstart += state->dx1*dy;
                state->xend   += state->dx2*dy;

                ystart = params->clipLowY;
        }

        if ((params->fbzMode & 1) && (yend > params->clipHighY))
                yend = params->clipHighY;

        state->y = ystart;
//        yend--;

#ifndef NO_CODEGEN
        if (voodoo->use_recompiler)
                voodoo_draw = voodoo_get_block(voodoo, params, state, odd_even);
#endif
              
#if 0
        if (voodoo_output)
                pclog("dxAB=%08x dxBC=%08x dxAC=%08x\n", state->dxAB, state->dxBC, state->dxAC);
#endif
//        pclog("Start %i %i\n", ystart, voodoo->fbzMode & (1 << 17));
        for (; state->y < yend; state->y++)
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

                if ((real_y & voodoo->odd_even_mask) != odd_even)
                        goto next_line;

                start_x = x;

                if (state->xdir > 0)
                        x2 -= (1 << 16);
                else
                        x -= (1 << 16);
                dx = ((x + 0x7000) >> 16) - (((state->vertexAx << 12) + 0x7000) >> 16);
                start_x2 = x + 0x7000;
                x = (x + 0x7000) >> 16;
                x2 = (x2 + 0x7000) >> 16;

#if 0
                if (voodoo_output)
                        pclog("%03i:%03i : Ax=%08x start_x=%08x  dSdX=%016llx dx=%08x  s=%08x -> ", x, state->y, state->vertexAx << 8, start_x, params->tmu[0].dTdX, dx, state->tmu0_t);
#endif
                        
                state->ir += (params->dRdX * dx);
                state->ig += (params->dGdX * dx);
                state->ib += (params->dBdX * dx);
                state->ia += (params->dAdX * dx);
                state->z += (params->dZdX * dx);
                state->tmu0_s += (params->tmu[0].dSdX * dx);
                state->tmu0_t += (params->tmu[0].dTdX * dx);
                state->tmu0_w += (params->tmu[0].dWdX * dx);
                state->w += (params->dWdX * dx);

#if 0
                if (voodoo_output)
                        pclog("%08llx %lli %lli\n", state->tmu0_t, state->tmu0_t >> (18+state->lod), (state->tmu0_t + (1 << 17+state->lod)) >> (18+state->lod));
#endif

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
                                        state->w += params->dWdX*dx;
                                        
                                        x = params->clipLeft;
                                }
                                if (x2 > params->clipRight)
                                        x2 = params->clipRight;
                        }
                        else
                        {
                                if (x > params->clipRight)
                                {
                                        int dx = params->clipRight - x;

                                        state->ir += params->dRdX*dx;
                                        state->ig += params->dGdX*dx;
                                        state->ib += params->dBdX*dx;
                                        state->ia += params->dAdX*dx;
                                        state->z += params->dZdX*dx;
                                        state->tmu0_s += params->tmu[0].dSdX*dx;
                                        state->tmu0_t += params->tmu[0].dTdX*dx;
                                        state->tmu0_w += params->tmu[0].dWdX*dx;
                                        state->w += params->dWdX*dx;
                                        
                                        x = params->clipRight;
                                }
                                if (x2 < params->clipLeft)
                                        x2 = params->clipLeft;
                        }
                }
                
                if (x2 < x && state->xdir > 0)
                        goto next_line;
                if (x2 > x && state->xdir < 0)
                        goto next_line;

                state->fb_mem = fb_mem = &voodoo->fb_mem[params->draw_offset + (real_y * voodoo->row_width)];
                state->aux_mem = aux_mem = &voodoo->fb_mem[params->aux_offset + (real_y * voodoo->row_width)];
                
#if 0
                if (voodoo_output)
                        pclog("%03i: x=%08x x2=%08x xstart=%08x xend=%08x dx=%08x start_x2=%08x\n", state->y, x, x2, state->xstart, state->xend, dx, start_x2);
#endif

                state->pixel_count = 0;
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
                        voodoo->pixel_count[odd_even]++;
                        voodoo->fbiPixelsIn++;
#if 0
                        if (voodoo_output)
                                pclog("  X=%03i T=%08x\n", x, state->tmu0_t);
#endif
//                        if (voodoo->fbzMode & FBZ_RGB_WMASK)
                        {
                                int update = 1;
                                uint8_t cother_r, cother_g, cother_b, aother;
                                uint8_t clocal_r, clocal_g, clocal_b, alocal;
                                int src_r, src_g, src_b, src_a;
                                int msel_r, msel_g, msel_b, msel_a;
                                uint8_t dest_r, dest_g, dest_b, dest_a;
                                uint16_t dat;
                                uint16_t aux_dat;
                                int sel;
                                int32_t new_depth, w_depth;

                                if (state->w & 0xffff00000000)
                                        w_depth = 0;
                                else if (!(state->w & 0xffff0000))
                                        w_depth = 0xf001;
                                else
                                {
                                        int exp = fls((uint16_t)((uint32_t)state->w >> 16));
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
                                        new_depth = (new_depth + params->zaColor) & 0xffff;
                                
                                if (params->fbzMode & FBZ_DEPTH_ENABLE)
                                {
                                        uint16_t old_depth = aux_mem[x];

                                        DEPTH_TEST();
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
                                        if (params->textureMode & 1)
                                        {
                                                int64_t _w = 0;
                                                if (state->tmu0_w)
                                                        _w = (int64_t)((1ULL << 48) / state->tmu0_w);

                                                state->tex_s = (int32_t)(((state->tmu0_s >> 14) * _w) >> 30);
                                                state->tex_t = (int32_t)(((state->tmu0_t >> 14)  * _w) >> 30);
//                                        state->lod = state->tmu[0].lod + (int)(log2((double)_w / (double)(1 << 19)) * 256.0);
                                                state->lod = state->tmu[0].lod + (fastlog(_w) - (19 << 8));
                                        }
                                        else
                                        {
                                                state->tex_s = (int32_t)(state->tmu0_s >> (14+14));
                                                state->tex_t = (int32_t)(state->tmu0_t >> (14+14));
                                                state->lod = state->tmu[0].lod;
                                        }
                                
                                        if (state->lod < state->lod_min)
                                                state->lod = state->lod_min;
                                        else if (state->lod > state->lod_max)
                                                state->lod = state->lod_max;
                                        state->lod >>= 8;

                                        voodoo_get_texture(voodoo, params, state);

                                        if ((params->fbzMode & FBZ_CHROMAKEY) &&
                                                state->tex_r == params->chromaKey_r &&
                                                state->tex_g == params->chromaKey_g &&
                                                state->tex_b == params->chromaKey_b)
                                        {
                                                voodoo->fbiChromaFail++;
                                                goto skip_pixel;
                                        }
                                }

                                if (voodoo->trexInit1 & (1 << 18))
                                {
                                        state->tex_r = state->tex_g = 0;
                                        state->tex_b = 1;
                                }

                                if (cc_localselect_override)
                                        sel = (state->tex_a & 0x80) ? 1 : 0;
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
                                        cother_r = state->tex_r;
                                        cother_g = state->tex_g;
                                        cother_b = state->tex_b;
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
                                        alocal = 0xff;
                                        break;
                                }
                                
                                switch (a_sel)
                                {
                                        case A_SEL_ITER_A:
                                        aother = CLAMP(state->ia >> 12);
                                        break;
                                        case A_SEL_TEX:
                                        aother = state->tex_a;
                                        break;
                                        case A_SEL_COLOR1:
                                        aother = (params->color1 >> 24) & 0xff;
                                        break;
                                        default:
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
                                        msel_r = state->tex_a;
                                        msel_g = state->tex_a;
                                        msel_b = state->tex_a;
                                        break;

                                        default:
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
                                        msel_a = state->tex_a;
                                        break;

                                        default:
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
                                        APPLY_FOG(src_r, src_g, src_b, state->z, state->ia);
                                
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
                                        if (params->fbzMode & FBZ_DEPTH_WMASK)
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
                                state->w -= params->dWdX;
                        }

                        x += state->xdir;
                } while (start_x != x2);

                voodoo->pixel_count[odd_even] += state->pixel_count;
                voodoo->fbiPixelsIn += state->pixel_count;
                
                if (voodoo->params.draw_offset == voodoo->params.front_offset)
                        voodoo->dirty_line[real_y] = 1;
next_line:
                state->base_r += params->dRdY;
                state->base_g += params->dGdY;
                state->base_b += params->dBdY;
                state->base_a += params->dAdY;
                state->base_z += params->dZdY;
                state->tmu[0].base_s += params->tmu[0].dSdY;
                state->tmu[0].base_t += params->tmu[0].dTdY;
                state->tmu[0].base_w += params->tmu[0].dWdY;
                state->base_w += params->dWdY;
                state->xstart += state->dx1;
                state->xend += state->dx2;
        }
}
        
static void voodoo_triangle(voodoo_t *voodoo, voodoo_params_t *params, int odd_even)
{
        voodoo_state_t state;
        int vertexAy_adjusted;
        int vertexBy_adjusted;
        int vertexCy_adjusted;
        int dx, dy;
        
        uint64_t tempdx, tempdy;
        uint64_t tempLOD;
        int LOD;
        int lodbias;
        
        voodoo->tri_count++;
        
        dx = 8 - (params->vertexAx & 0xf);
        if ((params->vertexAx & 0xf) > 8)
                dx += 16;
        dy = 8 - (params->vertexAy & 0xf);
        if ((params->vertexAy & 0xf) > 8)
                dy += 16;

/*        pclog("voodoo_triangle %i %i %i : vA %f, %f  vB %f, %f  vC %f, %f f %i %08x %08x %08x\n", odd_even, voodoo->params_read_idx[odd_even], voodoo->params_read_idx[odd_even] & PARAM_MASK, (float)params->vertexAx / 16.0, (float)params->vertexAy / 16.0, 
                                                                     (float)params->vertexBx / 16.0, (float)params->vertexBy / 16.0, 
                                                                     (float)params->vertexCx / 16.0, (float)params->vertexCy / 16.0, (params->fbzColorPath & FBZCP_TEXTURE_ENABLED) ? params->tformat : 0, params->fbzColorPath, params->alphaMode, params->textureMode);*/

        state.base_r = params->startR;
        state.base_g = params->startG;
        state.base_b = params->startB;
        state.base_a = params->startA;
        state.base_z = params->startZ;
        state.tmu[0].base_s = params->tmu[0].startS;
        state.tmu[0].base_t = params->tmu[0].startT;
        state.tmu[0].base_w = params->tmu[0].startW;
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
        vertexBy_adjusted = (state.vertexBy+7) >> 4;
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

        state.lod_min = (params->tLOD & 0x3f) << 6;
        state.lod_max = ((params->tLOD >> 6) & 0x3f) << 6;
        if (state.lod_max > 0x800)
                state.lod_max = 0x800;
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

        lodbias = (params->tLOD >> 12) & 0x3f;
        if (lodbias & 0x20)
                lodbias |= ~0x3f;
        state.tmu[0].lod = LOD + (lodbias << 6);

        state.palette = params->palette;

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
                        uint64_t start_time = timer_read();
                        uint64_t end_time;
                        voodoo_params_t *params = &voodoo->params_buffer[voodoo->params_read_idx[odd_even] & PARAM_MASK];
                        
                        voodoo_triangle(voodoo, params, odd_even);

                        voodoo->params_read_idx[odd_even]++;                                                
                        
                        if ((odd_even ? PARAM_ENTRIES_2 : PARAM_ENTRIES_1) > (PARAM_SIZE - 10))
                                thread_set_event(voodoo->render_not_full_event[odd_even]);

                        end_time = timer_read();
                        voodoo_render_time[odd_even] += end_time - start_time;
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

        memcpy(params_new, params, sizeof(voodoo_params_t) - sizeof(voodoo->palette));

        /*Copy palette data if required*/
        switch (params->tformat)
        {
                case TEX_PAL8: case TEX_APAL88:
                memcpy(params_new->palette, voodoo->palette, sizeof(voodoo->palette));
                break;
                case TEX_Y4I2Q2:
                memcpy(params_new->palette, voodoo->ncc_lookup[(params->textureMode & TEXTUREMODE_NCC_SEL) ? 1 : 0], sizeof(voodoo->palette));
                break;
        }
        
        voodoo->params_write_idx++;
        
        if (PARAM_ENTRIES_1 < 4 || (voodoo->render_threads == 2 && PARAM_ENTRIES_2 < 4))
                wake_render_thread(voodoo);
}

static void voodoo_fastfill(voodoo_t *voodoo, voodoo_params_t *params)
{
        int y;

        if (params->fbzMode & FBZ_RGB_WMASK)
        {
                int r, g, b;
                uint16_t col;

                r = ((params->color1 >> 16) >> 3) & 0x1f;
                g = ((params->color1 >> 8) >> 2) & 0x3f;
                b = (params->color1 >> 3) & 0x1f;
                col = b | (g << 5) | (r << 11);

                for (y = params->clipLowY; y < params->clipHighY; y++)
                {
                        uint16_t *cbuf = (uint16_t *)&voodoo->fb_mem[(params->draw_offset + y*voodoo->row_width) & voodoo->fb_mask];
                        int x;
                
                        for (x = params->clipLeft; x < params->clipRight; x++)
                                cbuf[x] = col;
                }
        }
        if (params->fbzMode & FBZ_DEPTH_WMASK)
        {        
                for (y = params->clipLowY; y < params->clipHighY; y++)
                {
                        uint16_t *abuf = (uint16_t *)&voodoo->fb_mem[(params->aux_offset + y*voodoo->row_width) & voodoo->fb_mask];
                        int x;
                
                        for (x = params->clipLeft; x < params->clipRight; x++)
                                abuf[x] = params->zaColor & 0xffff;
                }
        }
}

static uint32_t voodoo_reg_readl(uint32_t addr, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        uint32_t temp;
        
        switch (addr & 0x3fc)
        {
                
                case SST_lfbMode:
                temp = voodoo->lfbMode;
                break;
                
                case SST_fbiInit4:
                temp = voodoo->fbiInit4;
                break;
                case SST_fbiInit0:
                temp = voodoo->fbiInit0;
                break;
                case SST_fbiInit1:
                temp = voodoo->fbiInit1 & ~5; /*Pass-thru board with one SST-1*/
                break;              
                case SST_fbiInit2:
                if (voodoo->initEnable & 0x04)
                        temp = voodoo->dac_readdata;
                else
                        temp = voodoo->fbiInit2;
                break;
                case SST_fbiInit3:
                temp = voodoo->fbiInit3;
                break;
                
                default:
                fatal("voodoo_readl  : bad addr %08X\n", addr);
                temp = 0xffffffff;
        }

        return temp;
}

enum
{
        CHIP_FBI = 0x1,
        CHIP_TREX0 = 0x2,
        CHIP_TREX1 = 0x2,
        CHIP_TREX2 = 0x2
};

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
//pclog("voodoo_write_l: addr=%08x val=%08x(%f) chip=%x\n", addr, val, tempif.f, chip);
        addr &= 0x3fc;

        if ((voodoo->fbiInit3 & FBIINIT3_REMAP) && addr < 0x100 && ad21)
                addr |= 0x400;
        switch (addr)
        {
                case SST_swapbufferCMD:
//                pclog("  start swap buffer command\n");

                voodoo->disp_buffer = !voodoo->disp_buffer;
                voodoo_recalc(voodoo);

                voodoo->params.swapbufferCMD = val;

#if 0
                pclog("Swap buffer %08x %d %p\n", val, voodoo->swap_count, &voodoo->swap_count);
#endif
//                voodoo->front_offset = params->front_offset;
                wait_for_render_thread_idle(voodoo);
                if (!(val & 1))
                {
                        memset(voodoo->dirty_line, 1, 1024);
                        voodoo->front_offset = voodoo->params.front_offset;
                        voodoo->swap_count--;
                }
                else
                {
                        voodoo->swap_interval = (val >> 1) & 0xff;
                        voodoo->swap_offset = voodoo->params.front_offset;
                        voodoo->swap_pending = 1;

                        while (voodoo->swap_pending)
                        {
                                thread_wait_event(voodoo->wake_fifo_thread, -1);
                                thread_reset_event(voodoo->wake_fifo_thread);
                                if ((voodoo->swap_pending && voodoo->flush) || FIFO_ENTRIES == 65536)
                                {
                                        /*Main thread is waiting for FIFO to empty, so skip vsync wait and just swap*/
                                        memset(voodoo->dirty_line, 1, 1024);
                                        voodoo->front_offset = voodoo->params.front_offset;
                                        voodoo->swap_count--;
                                        voodoo->swap_pending = 0;
                                        break;
                                }
                        }
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
                break;
                case SST_startT: case SST_remap_startT:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startT = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_startW: case SST_remap_startW:
                if (chip & CHIP_FBI)
                        voodoo->params.startW = (int64_t)(int32_t)val << 2;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startW = (int64_t)(int32_t)val << 2;
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
                break;
                case SST_dTdX: case SST_remap_dTdX:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dTdX = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_dWdX: case SST_remap_dWdX:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dWdX = (int64_t)(int32_t)val << 2;                
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
                break;
                case SST_dTdY: case SST_remap_dTdY:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dTdY = ((int64_t)(int32_t)val) << 14;
                break;
                case SST_dWdY: case SST_remap_dWdY:
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dWdY = (int64_t)(int32_t)val << 2;
                if (chip & CHIP_FBI)
                        voodoo->params.dWdY = (int64_t)(int32_t)val << 2;
                break;

                case SST_triangleCMD: case SST_remap_triangleCMD:
                voodoo->params.sign = val & (1 << 31);

                if (voodoo->ncc_dirty)
                        voodoo_update_ncc(voodoo);
                voodoo->ncc_dirty = 0;

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
                break;
                case SST_fstartT: case SST_remap_fstartT:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startT = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fstartW: case SST_remap_fstartW:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].startW = (int64_t)(tempif.f * 4294967296.0f);
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
                break;
                case SST_fdTdX: case SST_remap_fdTdX:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dTdX = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fdWdX: case SST_remap_fdWdX:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dWdX = (int64_t)(tempif.f * 4294967296.0f);
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
                break;
                case SST_fdTdY: case SST_remap_fdTdY:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dTdY = (int64_t)(tempif.f * 4294967296.0f);
                break;
                case SST_fdWdY: case SST_remap_fdWdY:
                tempif.i = val;
                if (chip & CHIP_TREX0)
                        voodoo->params.tmu[0].dWdY = (int64_t)(tempif.f * 4294967296.0f);
                if (chip & CHIP_FBI)
                        voodoo->params.dWdY = (int64_t)(tempif.f * 4294967296.0f);
                break;

                case SST_ftriangleCMD:
                voodoo->params.sign = val & (1 << 31);

                if (voodoo->ncc_dirty)
                        voodoo_update_ncc(voodoo);
                voodoo->ncc_dirty = 0;

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
                voodoo->params.clipRight = val & 0x3ff;
                voodoo->params.clipLeft = (val >> 16) & 0x3ff;
                break;
                case SST_clipLowYHighY:
                voodoo->params.clipHighY = val & 0x3ff;
                voodoo->params.clipLowY = (val >> 16) & 0x3ff;
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

                case SST_textureMode:
                voodoo->params.textureMode = val;
                voodoo->params.tformat = (val >> 8) & 0xf;
                break;
                case SST_tLOD:
                voodoo->params.tLOD = val;
                voodoo_recalc_tex(voodoo);
                break;

                case SST_texBaseAddr:
//                pclog("Write texBaseAddr %08x\n", val);
                voodoo->params.texBaseAddr = (val & 0x7ffff) << 3;
                voodoo_recalc_tex(voodoo);
                break;
                case SST_texBaseAddr1:
                voodoo->params.texBaseAddr1 = (val & 0x7ffff) << 3;
                voodoo_recalc_tex(voodoo);
                break;
                case SST_texBaseAddr2:
                voodoo->params.texBaseAddr2 = (val & 0x7ffff) << 3;
                voodoo_recalc_tex(voodoo);
                break;
                case SST_texBaseAddr38:
                voodoo->params.texBaseAddr38 = (val & 0x7ffff) << 3;
                voodoo_recalc_tex(voodoo);
                break;
                
                case SST_trexInit1:
                voodoo->trexInit1 = val;
                break;

                case SST_nccTable0_Y0:
                voodoo->nccTable[0].y[0] = val;
                voodoo->ncc_dirty = 1;
                break;
                case SST_nccTable0_Y1:
                voodoo->nccTable[0].y[1] = val;
                voodoo->ncc_dirty = 1;
                break;
                case SST_nccTable0_Y2:
                voodoo->nccTable[0].y[2] = val;
                voodoo->ncc_dirty = 1;
                break;
                case SST_nccTable0_Y3:
                voodoo->nccTable[0].y[3] = val;
                voodoo->ncc_dirty = 1;
                break;
                
                case SST_nccTable0_I0:
                if (!(val & (1 << 31)))
                {
                        voodoo->nccTable[0].i[0] = val;
                        voodoo->ncc_dirty = 1;
                        break;
                }
                case SST_nccTable0_I2:
                if (!(val & (1 << 31)))
                {
                        voodoo->nccTable[0].i[2] = val;
                        voodoo->ncc_dirty = 1;
                        break;
                }
                case SST_nccTable0_Q0:
                if (!(val & (1 << 31)))
                {
                        voodoo->nccTable[0].q[0] = val;
                        voodoo->ncc_dirty = 1;
                        break;
                }
                case SST_nccTable0_Q2:
                if (!(val & (1 << 31)))
                {
                        voodoo->nccTable[0].q[2] = val;
                        voodoo->ncc_dirty = 1;
                        break;
                }
                if (val & (1 << 31))
                {
                        int p = (val >> 23) & 0xfe;
                        voodoo->palette[p].u = val | 0xff000000;
                }
                break;
                        
                case SST_nccTable0_I1:
                if (!(val & (1 << 31)))
                {
                        voodoo->nccTable[0].i[1] = val;
                        voodoo->ncc_dirty = 1;
                        break;
                }
                case SST_nccTable0_I3:
                if (!(val & (1 << 31)))
                {
                        voodoo->nccTable[0].i[3] = val;
                        voodoo->ncc_dirty = 1;
                        break;
                }
                case SST_nccTable0_Q1:
                if (!(val & (1 << 31)))
                {
                        voodoo->nccTable[0].q[1] = val;
                        voodoo->ncc_dirty = 1;
                        break;
                }
                case SST_nccTable0_Q3:
                if (!(val & (1 << 31)))
                {
                        voodoo->nccTable[0].q[3] = val;
                        voodoo->ncc_dirty = 1;
                        break;
                }
                if (val & (1 << 31))
                {
                        int p = ((val >> 23) & 0xfe) | 0x01;
                        voodoo->palette[p].u = val | 0xff000000;
                }
                break;

                case SST_nccTable1_Y0:
                voodoo->nccTable[1].y[0] = val;
                voodoo->ncc_dirty = 1;
                break;
                case SST_nccTable1_Y1:
                voodoo->nccTable[1].y[1] = val;
                voodoo->ncc_dirty = 1;
                break;
                case SST_nccTable1_Y2:
                voodoo->nccTable[1].y[2] = val;
                voodoo->ncc_dirty = 1;
                break;
                case SST_nccTable1_Y3:
                voodoo->nccTable[1].y[3] = val;
                voodoo->ncc_dirty = 1;
                break;
                case SST_nccTable1_I0:
                voodoo->nccTable[1].i[0] = val;
                voodoo->ncc_dirty = 1;
                break;
                case SST_nccTable1_I1:
                voodoo->nccTable[1].i[1] = val;
                voodoo->ncc_dirty = 1;
                break;
                case SST_nccTable1_I2:
                voodoo->nccTable[1].i[2] = val;
                voodoo->ncc_dirty = 1;
                break;
                case SST_nccTable1_I3:
                voodoo->nccTable[1].i[3] = val;
                voodoo->ncc_dirty = 1;
                break;
                case SST_nccTable1_Q0:
                voodoo->nccTable[1].q[0] = val;
                voodoo->ncc_dirty = 1;
                break;
                case SST_nccTable1_Q1:
                voodoo->nccTable[1].q[1] = val;
                voodoo->ncc_dirty = 1;
                break;
                case SST_nccTable1_Q2:
                voodoo->nccTable[1].q[2] = val;
                voodoo->ncc_dirty = 1;
                break;
                case SST_nccTable1_Q3:
                voodoo->nccTable[1].q[3] = val;
                voodoo->ncc_dirty = 1;
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
        read_addr = voodoo->fb_read_offset + (x << 1) + (y * voodoo->row_width);

        if (read_addr > voodoo->fb_mask)
                return 0xffff;

        temp = *(uint16_t *)(&voodoo->fb_mem[read_addr & voodoo->fb_mask]);

//        pclog("voodoo_fb_readw : %08X %08X  %i %i  %08X %08X  %08x:%08x %i\n", addr, temp, x, y, read_addr, *(uint32_t *)(&voodoo->fb_mem[4]), cs, pc, fb_reads++);
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
        read_addr = voodoo->fb_read_offset + x + (y * voodoo->row_width);

        if (read_addr > voodoo->fb_mask)
                return 0xffffffff;

        temp = *(uint32_t *)(&voodoo->fb_mem[read_addr & voodoo->fb_mask]);

//        pclog("voodoo_fb_readl : %08X %08x %08X  x=%i y=%i  %08X %08X  %08x:%08x %i ro=%08x rw=%i\n", addr, read_addr, temp, x, y, read_addr, *(uint32_t *)(&voodoo->fb_mem[4]), cs, pc, fb_reads++, voodoo->fb_read_offset, voodoo->row_width);
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
        int write_mask;

        depth_data = voodoo->params.zaColor & 0xffff;
        alpha_data = voodoo->params.zaColor >> 24;
        
//        while (!RB_EMPTY)
//                thread_reset_event(voodoo->not_full_event);
        
//        pclog("voodoo_fb_writew : %08X %04X\n", addr, val);
        
               
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
                
                default:
                fatal("voodoo_fb_writew : bad LFB format %08X\n", voodoo->lfbMode);
        }

        x = addr & 0x7fe;
        y = (addr >> 11) & 0x3ff;
        
        if (voodoo->fb_write_offset == voodoo->params.front_offset)
                voodoo->dirty_line[y] = 1;
        
        write_addr = voodoo->fb_write_offset + x + (y * voodoo->row_width);
        write_addr_aux = voodoo->params.aux_offset + x + (y * voodoo->row_width);
        
//        pclog("fb_writew %08x %i %i %i %08x\n", addr, x, y, voodoo->row_width, write_addr);

        if (voodoo->lfbMode & 0x100)
        {
                {
                        rgba8_t write_data = colour_data;
                        uint16_t new_depth = depth_data;

                        if (params->fbzMode & FBZ_DEPTH_ENABLE)
                        {
                                uint16_t old_depth = *(uint16_t *)(&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]);

                                DEPTH_TEST();
                        }

                        if ((params->fbzMode & FBZ_CHROMAKEY) &&
                                write_data.r == params->chromaKey_r &&
                                write_data.g == params->chromaKey_g &&
                                write_data.b == params->chromaKey_b)
                                goto skip_pixel;

                        if (params->fogMode & FOG_ENABLE)
                        {
                                int32_t z = new_depth << 12;
                                int32_t w_depth = new_depth;
                                int32_t ia = alpha_data << 12;

                                APPLY_FOG(write_data.r, write_data.g, write_data.b, z, ia);
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
                        x = x;
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
        int write_mask, count = 1;

        depth_data[0] = depth_data[1] = voodoo->params.zaColor & 0xffff;
        alpha_data[0] = alpha_data[1] = voodoo->params.zaColor >> 24;
//        while (!RB_EMPTY)
//                thread_reset_event(voodoo->not_full_event);
        
//        pclog("voodoo_fb_writel : %08X %08X\n", addr, val);
        
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

        if (voodoo->fb_write_offset == voodoo->params.front_offset)
                voodoo->dirty_line[y] = 1;
        
        write_addr = voodoo->fb_write_offset + x + (y * voodoo->row_width);
        write_addr_aux = voodoo->params.aux_offset + x + (y * voodoo->row_width);
        
//        pclog("fb_writel %08x x=%i y=%i rw=%i %08x wo=%08x\n", addr, x, y, voodoo->row_width, write_addr, voodoo->fb_write_offset);

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

                                DEPTH_TEST();
                        }

                        if ((params->fbzMode & FBZ_CHROMAKEY) &&
                                write_data.r == params->chromaKey_r &&
                                write_data.g == params->chromaKey_g &&
                                write_data.b == params->chromaKey_b)
                                goto skip_pixel;

                        if (params->fogMode & FOG_ENABLE)
                        {
                                int32_t z = new_depth << 12;
                                int32_t w_depth = new_depth;
                                int32_t ia = alpha_data[c] << 12;

                                APPLY_FOG(write_data.r, write_data.g, write_data.b, z, ia);
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

        if (addr & 0x600000)
                return; /*TREX != 0*/

//        pclog("voodoo_tex_writel : %08X %08X %i\n", addr, val, voodoo->params.tformat);
        
        lod = (addr >> 17) & 0xf;
        t = (addr >> 9) & 0xff;
        if (voodoo->params.tformat & 8)
                s = (addr >> 1) & 0xfe;
        else
        {
                if (voodoo->params.textureMode & (1 << 31))
                        s = addr & 0xfc;
                else
                        s = (addr >> 1) & 0xfc;
        }

        if (lod > LOD_MAX)
                return;
        
//        if (addr >= 0x200000)
//                return;
        
        if (voodoo->params.tformat & 8)
                addr = voodoo->params.tex_base[lod] + s*2 + (t << voodoo->params.tex_shift[lod])*2;
        else
                addr = voodoo->params.tex_base[lod] + s + (t << voodoo->params.tex_shift[lod]);
        *(uint32_t *)(&voodoo->tex_mem[addr & voodoo->texture_mask]) = val;
}

static inline void wake_fifo_thread(voodoo_t *voodoo)
{
        thread_set_event(voodoo->wake_fifo_thread); /*Wake up FIFO thread if moving from idle*/
}

static inline void queue_command(voodoo_t *voodoo, uint32_t addr_type, uint32_t val)
{
        fifo_entry_t *fifo = &voodoo->fifo[voodoo->fifo_write_idx & FIFO_MASK];
        int c;

        if (FIFO_FULL)
        {
                thread_reset_event(voodoo->fifo_not_full_event);
                if (FIFO_FULL)
                {
                        thread_wait_event(voodoo->fifo_not_full_event, -1); /*Wait for room in ringbuffer*/
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

        cycles -= pci_nonburst_time;
        
        if ((addr & 0xc00000) == 0x400000) /*Framebuffer*/
        {
                voodoo->flush = 1;
                while (!FIFO_EMPTY)
                {
                        wake_fifo_thread(voodoo);
                        thread_wait_event(voodoo->fifo_not_full_event, 1);
                }
                wait_for_render_thread_idle(voodoo);
                voodoo->flush = 0;
                
                return voodoo_fb_readw(addr, voodoo);
        }

        return 0xffff;
}

static uint32_t voodoo_readl(uint32_t addr, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        uint32_t temp;
        int fifo_size;
        voodoo->rd_count++;
        addr &= 0xffffff;
        
        cycles -= pci_nonburst_time;
        
        if (addr & 0x800000) /*Texture*/
        {
        }
        else if (addr & 0x400000) /*Framebuffer*/
        {
                voodoo->flush = 1;
                while (!FIFO_EMPTY)
                {
                        wake_fifo_thread(voodoo);
                        thread_wait_event(voodoo->fifo_not_full_event, 1);
                }
                wait_for_render_thread_idle(voodoo);
                voodoo->flush = 0;
                
                temp = voodoo_fb_readl(addr, voodoo);
        }
        else switch (addr & 0x3fc)
        {
                case SST_status:
                fifo_size = 0xffff - FIFO_ENTRIES;
                temp = fifo_size << 12;
                if (fifo_size < 0x40)
                        temp |= fifo_size;
                else
                        temp |= 0x3f;
                temp |= (voodoo->swap_count << 28);
                if (voodoo->cmd_written - voodoo->cmd_read)
                        temp |= 0x380; /*Busy*/
                if (!voodoo->v_retrace)
                        temp |= 0x40;
                if (!voodoo->voodoo_busy)
                        wake_fifo_thread(voodoo);
                break;
                
                case SST_lfbMode:
                voodoo->flush = 1;
                while (!FIFO_EMPTY)
                {
                        wake_fifo_thread(voodoo);
                        thread_wait_event(voodoo->fifo_not_full_event, 1);
                }
                wait_for_render_thread_idle(voodoo);
                voodoo->flush = 0;

                temp = voodoo->lfbMode;
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
                temp = voodoo->fbiInit1 & ~5; /*Pass-thru board with one SST-1*/
                break;              
                case SST_fbiInit2:
                if (voodoo->initEnable & 0x04)
                        temp = voodoo->dac_readdata;
                else
                        temp = voodoo->fbiInit2;
                break;
                case SST_fbiInit3:
                temp = voodoo->fbiInit3;
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
                cycles -= pci_burst_time;
        else
                cycles -= pci_nonburst_time;
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
        
//        pclog("Pixel clock %f MHz hsync %08x line_length %d\n", t, voodoo->hSync, line_length);
        
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
                cycles -= pci_burst_time;
        else
                cycles -= pci_nonburst_time;
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
        else switch (addr & 0x3fc)
        {
                case SST_swapbufferCMD:
                voodoo->cmd_written++;
                voodoo->swap_count++;
                queue_command(voodoo, addr | FIFO_WRITEL_REG, val);
                if (!voodoo->voodoo_busy)
                        wake_fifo_thread(voodoo);
                break;
                case SST_triangleCMD:
                voodoo->cmd_written++;
                queue_command(voodoo, addr | FIFO_WRITEL_REG, val);
                if (!voodoo->voodoo_busy)
                        wake_fifo_thread(voodoo);
                break;
                case SST_ftriangleCMD:
                voodoo->cmd_written++;
                queue_command(voodoo, addr | FIFO_WRITEL_REG, val);
                if (!voodoo->voodoo_busy)
                        wake_fifo_thread(voodoo);
                break;
                case SST_fastfillCMD:
                voodoo->cmd_written++;
                queue_command(voodoo, addr | FIFO_WRITEL_REG, val);
                if (!voodoo->voodoo_busy)
                        wake_fifo_thread(voodoo);
                break;
                case SST_nopCMD:
                voodoo->cmd_written++;
                queue_command(voodoo, addr | FIFO_WRITEL_REG, val);
                if (!voodoo->voodoo_busy)
                        wake_fifo_thread(voodoo);
                break;
                        
                case SST_fbiInit4:
                if (voodoo->initEnable & 0x01)
                        voodoo->fbiInit4 = val;
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
                        svga_set_override(voodoo->svga, val & 1);
                }
                break;
                case SST_fbiInit1:
                if (voodoo->initEnable & 0x01)
                        voodoo->fbiInit1 = val;
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
                voodoo->clutData_dirty = 1;
                break;

                case SST_dacData:
                voodoo->dac_reg = (val >> 8) & 7;
                voodoo->dac_readdata = 0xff;
                if (val & 0x800)
                {
//                        pclog("  dacData read %i %02X\n", voodoo->dac_reg, voodoo->dac_data[7]);
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
                                voodoo->dac_readdata = voodoo->dac_data[voodoo->dac_reg];
                }
                else
                {
                        if (voodoo->dac_reg == 5)
                        {
                                if (!voodoo->dac_reg_ff)
                                        voodoo->dac_pll_regs[voodoo->dac_data[4] & 0xf] = (voodoo->dac_pll_regs[voodoo->dac_data[4] & 0xf] & 0xff00) | val;
                                else
                                        voodoo->dac_pll_regs[voodoo->dac_data[4] & 0xf] = (voodoo->dac_pll_regs[voodoo->dac_data[4] & 0xf] & 0xff) | (val << 8);
//                                pclog("Write PLL reg %x %04x\n", voodoo->dac_data[4] & 0xf, voodoo->dac_pll_regs[voodoo->dac_data[4] & 0xf]);
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

                default:
                queue_command(voodoo, addr | FIFO_WRITEL_REG, val);
                break;
        }
}

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
                        uint64_t start_time = timer_read();
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
                                if (!(fifo->addr_type & 0x600000))
                                {
                                        wait_for_render_thread_idle(voodoo);
                                        voodoo_tex_writel(fifo->addr_type & FIFO_ADDR, fifo->val, voodoo);
                                }
                                break;
                        }
                                                
                        voodoo->fifo_read_idx++;
                        fifo->addr_type = FIFO_INVALID;

                        if (FIFO_ENTRIES > 0xe000)
                                thread_set_event(voodoo->fifo_not_full_event);

                        end_time = timer_read();
                        voodoo_time += end_time - start_time;
                }
                voodoo->voodoo_busy = 0;

        }
}

static void voodoo_recalcmapping(voodoo_t *voodoo)
{
        if (voodoo->pci_enable && voodoo->memBaseAddr)
        {
#if 0
                pclog("voodoo_recalcmapping : memBaseAddr %08X\n", voodoo->memBaseAddr);
#endif
                mem_mapping_set_addr(&voodoo->mapping, voodoo->memBaseAddr, 0x01000000);
        }
        else
        {
#if 0
                pclog("voodoo_recalcmapping : disabled\n");
#endif
                mem_mapping_disable(&voodoo->mapping);
        }
}

uint8_t voodoo_pci_read(int func, int addr, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;

#if 0
        pclog("Voodoo PCI read %08X\n", addr);
#endif
        switch (addr)
        {
                case 0x00: return 0x1a; /*3dfx*/
                case 0x01: return 0x12;
                
                case 0x02: return 0x01; /*SST-1 (Voodoo Graphics)*/
                case 0x03: return 0x00;
                
                case 0x04: return voodoo->pci_enable ? 0x02 : 0x00; /*Respond to memory accesses*/

                case 0x08: return 2; /*Revision ID*/
                case 0x09: return 0; /*Programming interface*/
                
                case 0x10: return 0x00; /*memBaseAddr*/
                case 0x11: return 0x00;
                case 0x12: return 0x00;
                case 0x13: return voodoo->memBaseAddr >> 24;

                case 0x40: return voodoo->initEnable;
        }
        return 0;
}

void voodoo_pci_write(int func, int addr, uint8_t val, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        
#if 0
        pclog("Voodoo PCI write %04X %02X\n", addr, val);
#endif
        switch (addr)
        {
                case 0x04:
                voodoo->pci_enable = val & 2;
                voodoo_recalcmapping(voodoo);
                break;
                
                case 0x13:
                voodoo->memBaseAddr = val << 24;
                voodoo_recalcmapping(voodoo);
                break;
                
                case 0x40:
                voodoo->initEnable = val; 
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

#define FILTCAP 64.0f /* Needs tuning to match DAC */
#define FILTCAPG (FILTCAP / 2)

static void voodoo_generate_filter(voodoo_t *voodoo)
{
        int g, h, i;
        float difference, diffg;
        float color;
        float thiscol, thiscolg, lined;

        for (g=0;g<1024;g++)         // pixel 1
        {
                for (h=0;h<1024;h++)      // pixel 2
                {
                        difference = h - g;
                        diffg = difference / 2;

                        if (difference > FILTCAP)
                                difference = FILTCAP;
                        if (difference < -FILTCAP)
                                difference = -FILTCAP;

                        if (diffg > FILTCAPG)
                                diffg = FILTCAPG;
                        if (diffg < -FILTCAPG)
                                diffg = -FILTCAPG;

                        thiscol = g + (difference / 3);
                        thiscolg = g + (diffg / 3);

                        if (thiscol < 0)
                                thiscol = 0;
                        if (thiscol > 1023)
                                thiscol = 1023;

                        if (thiscolg < 0)
                                thiscolg = 0;
                        if (thiscolg > 1023)
                                thiscolg = 1023;

                        voodoo->thefilter[g][h] = thiscol;
                        voodoo->thefilterg[g][h] = thiscolg;
                }

                lined = g + 4;
                if (lined > 1023)
                        lined = 1023;
                voodoo->purpleline[g] = lined;
        }
}

static void voodoo_filterline(voodoo_t *voodoo, uint16_t *fil, int column, uint16_t *src, int line)
{
	int x;
	
	/* 16 to 32-bit */
        for (x=0; x<column;x++)
        {
                fil[x*3] = ((src[x] & 31) << 5);
                fil[x*3+1] = (((src[x] >> 5) & 63) << 4);
                fil[x*3+2] = (((src[x] >> 11) & 31) << 5);
        }
        fil[x*3] = 0;
        fil[x*3+1] = 0;
        fil[x*3+2] = 0;

        /* filtering time */

        for (x=1; x<column-1;x++)
        {
                fil[x*3]   = voodoo->thefilter[fil[x*3]][fil[(x-1)*3]];
                fil[x*3+1] = voodoo->thefilterg[fil[x*3+1]][fil[(x-1)*3+1]];
                fil[x*3+2] = voodoo->thefilter[fil[x*3+2]][fil[(x-1)*3+2]];
        }
        for (x=1; x<column-1;x++)
        {
                fil[x*3]   = voodoo->thefilter[fil[x*3]][fil[(x-1)*3]];
                fil[x*3+1] = voodoo->thefilterg[fil[x*3+1]][fil[(x-1)*3+1]];
                fil[x*3+2] = voodoo->thefilter[fil[x*3+2]][fil[(x-1)*3+2]];
        }
        for (x=1; x<column-1;x++)
        {
                fil[x*3]   = voodoo->thefilter[fil[x*3]][fil[(x-1)*3]];
                fil[x*3+1] = voodoo->thefilterg[fil[x*3+1]][fil[(x-1)*3+1]];
                fil[x*3+2] = voodoo->thefilter[fil[x*3+2]][fil[(x-1)*3+2]];
        }

        for (x=0; x<column;x++)
        {
                fil[x*3]   = (voodoo->thefilter[fil[x*3]][fil[(x+1)*3]]) >> 2;
                fil[x*3+1] = (voodoo->thefilterg[fil[x*3+1]][fil[(x+1)*3+1]]) >> 2;
                fil[x*3+2] = (voodoo->thefilter[fil[x*3+2]][fil[(x+1)*3+2]]) >> 2;
        }

        /* lines */

        if (line & 1)
        {
                for (x=0; x<column;x++)
                {
                        fil[x*3] = voodoo->purpleline[fil[x*3]];
                        fil[x*3+2] = voodoo->purpleline[fil[x*3+2]];
                }
        }
}

void voodoo_callback(void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
		int y_add = enable_overscan ? 16 : 0;
		int x_add = enable_overscan ? 8 : 0;

        if (voodoo->fbiInit0 & FBIINIT0_VGA_PASS)
        {
                if (voodoo->line < voodoo->v_disp)
                {
                        if (voodoo->dirty_line[voodoo->line])
                        {
                                uint32_t *p = &((uint32_t *)buffer32->line[voodoo->line + y_add])[32 + x_add];
                                uint16_t *src = (uint16_t *)&voodoo->fb_mem[voodoo->front_offset + voodoo->line*voodoo->row_width];
                                int x;

                                voodoo->dirty_line[voodoo->line] = 0;
                                
                                if (voodoo->line < voodoo->dirty_line_low)
                                {
                                        voodoo->dirty_line_low = voodoo->line;
                                        video_wait_for_buffer();
                                }
                                if (voodoo->line > voodoo->dirty_line_high)
                                        voodoo->dirty_line_high = voodoo->line;
                                
                                if (voodoo->scrfilter)
                                {
                                        int j, offset;
                                        uint16_t fil[(voodoo->h_disp + 1) * 3];              /* interleaved 24-bit RGB */

                                        voodoo_filterline(voodoo, fil, voodoo->h_disp, src, voodoo->line);

                                        for (x = 0; x < voodoo->h_disp; x++)
                                        {
                                                p[x] = (voodoo->clutData256[fil[x*3]].b << 0 | voodoo->clutData256[fil[x*3+1]].g << 8 | voodoo->clutData256[fil[x*3+2]].r << 16);
                                        }
                                }
                                else
                                {
                                        for (x = 0; x < voodoo->h_disp; x++)
                                        {
                                                p[x] = voodoo->video_16to32[src[x]];
                                        }
                                }
                        }
                }
        }
        if (voodoo->line == voodoo->v_disp)
        {
//                pclog("retrace %i %i %08x %i\n", voodoo->retrace_count, voodoo->swap_interval, voodoo->swap_offset, voodoo->swap_pending);
                voodoo->retrace_count++;
                if (voodoo->swap_pending && (voodoo->retrace_count > voodoo->swap_interval))
                {
                        memset(voodoo->dirty_line, 1, 1024);
                        voodoo->retrace_count = 0;
                        voodoo->front_offset = voodoo->swap_offset;
                        voodoo->swap_count--;
                        voodoo->swap_pending = 0;
                        thread_set_event(voodoo->wake_fifo_thread);
                        voodoo->frame_count++;
                }
                voodoo->v_retrace = 1;
        }
        voodoo->line++;
        
        if (voodoo->fbiInit0 & FBIINIT0_VGA_PASS)
        {
                if (voodoo->line == voodoo->v_disp)
                {
                        if (voodoo->dirty_line_high > voodoo->dirty_line_low)
                                svga_doblit(0, voodoo->v_disp, voodoo->h_disp, voodoo->v_disp, voodoo->svga);
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
        voodoo_t *voodoo = (voodoo_t *)p;
        char temps[256];
        int pixel_count_current[2];
        int pixel_count_total;
        uint64_t new_time = timer_read();
        uint64_t status_diff = new_time - status_time;
        status_time = new_time;

        if (!status_diff)
                status_diff = 1;

        svga_add_status_info(s, max_len, &voodoo->svga);
        
        pixel_count_current[0] = voodoo->pixel_count[0];
        pixel_count_current[1] = voodoo->pixel_count[1];
        pixel_count_total = (pixel_count_current[0] + pixel_count_current[1]) - (voodoo->pixel_count_old[0] + voodoo->pixel_count_old[1]);
        sprintf(temps, "%f Mpixels/sec (%f)\n%f ktris/sec\n%f%% CPU (%f%% real)\n%d frames/sec (%i)\n%f%% CPU (%f%% real)\n%f%% CPU (%f%% real)\n"/*%d reads/sec\n%d write/sec\n%d tex/sec\n*/,
                (double)pixel_count_total/1000000.0,
                ((double)pixel_count_total/1000000.0) / ((double)voodoo_render_time[0] / status_diff),
                (double)voodoo->tri_count/1000.0, ((double)voodoo_time * 100.0) / timer_freq, ((double)voodoo_time * 100.0) / status_diff, voodoo->frame_count, voodoo_recomp,
                ((double)voodoo_render_time[0] * 100.0) / timer_freq, ((double)voodoo_render_time[0] * 100.0) / status_diff,
                ((double)voodoo_render_time[1] * 100.0) / timer_freq, ((double)voodoo_render_time[1] * 100.0) / status_diff);
        strncat(s, temps, max_len);

        voodoo->pixel_count_old[0] = pixel_count_current[0];
        voodoo->pixel_count_old[1] = pixel_count_current[1];
        voodoo->tri_count = voodoo->frame_count = 0;
        voodoo->rd_count = voodoo->wr_count = voodoo->tex_count = 0;
        voodoo_time = 0;
        voodoo_render_time[0] = voodoo_render_time[1] = 0;
        voodoo_recomp = 0;
}

static void voodoo_speed_changed(void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        
        voodoo_pixelclock_update(voodoo);
}

void *voodoo_init()
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
        voodoo_generate_filter(voodoo);   /*generate filter lookup tables*/
        
        pci_add(voodoo_pci_read, voodoo_pci_write, voodoo);

        mem_mapping_add(&voodoo->mapping, 0, 0, NULL, voodoo_readw, voodoo_readl, NULL, voodoo_writew, voodoo_writel,     NULL, 0, voodoo);

        voodoo->fb_mem = malloc(4 * 1024 * 1024);
        voodoo->tex_mem = malloc(voodoo->texture_size * 1024 * 1024);
        voodoo->tex_mem_w = (uint16_t *)voodoo->tex_mem;

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
        return voodoo;
}

void voodoo_close(void *p)
{
        FILE *f;
        voodoo_t *voodoo = (voodoo_t *)p;
#ifndef RELEASE_BUILD        
        f = romfopen("texram.dmp", "wb");
        fwrite(voodoo->tex_mem, 2048*1024, 1, f);
        fclose(f);
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
#ifndef NO_CODEGEN
        voodoo_codegen_close(voodoo);
#endif
        free(voodoo->fb_mem);
        free(voodoo->tex_mem);
        free(voodoo);
}

static device_config_t voodoo_config[] =
{
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

device_t voodoo_device =
{
        "3DFX Voodoo Graphics",
        0,
        voodoo_init,
        voodoo_close,
        NULL,
        voodoo_speed_changed,
        NULL,
        voodoo_add_status_info,
        voodoo_config
};
