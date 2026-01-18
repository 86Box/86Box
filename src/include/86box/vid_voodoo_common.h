/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Voodoo Graphics, 2, Banshee, 3 emulation.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          leilei
 *
 *          Copyright 2008-2020 Sarah Walker.
 */
#ifndef VIDEO_VOODOO_COMMON_H
#define VIDEO_VOODOO_COMMON_H

#ifdef CLAMP
#    undef CLAMP
#endif

#define CLAMP(x)        (((x) < 0) ? 0 : (((x) > 0xff) ? 0xff : (x)))
#define CLAMP16(x)      (((x) < 0) ? 0 : (((x) > 0xffff) ? 0xffff : (x)))

#define LOD_MAX         8

#define TEX_DIRTY_SHIFT 10

#define TEX_CACHE_MAX   64

enum {
    VOODOO_1 = 0,
    VOODOO_SB50,
    VOODOO_2,
    VOODOO_BANSHEE,
    VOODOO_3
};

typedef union int_float {
    uint32_t i;
    float    f;
} int_float;

typedef struct rgbvoodoo_t {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t pad;
} rgbvoodoo_t;
typedef struct rgba8_t {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
} rgba8_t;

typedef union rgba_u {
    struct
    {
        uint8_t b;
        uint8_t g;
        uint8_t r;
        uint8_t a;
    } rgba;
    uint32_t u;
} rgba_u;

#define FIFO_SIZE       65536
#define FIFO_MASK       (FIFO_SIZE - 1)
#define FIFO_ENTRY_SIZE (1 << 31)

#define FIFO_ENTRIES    (voodoo->fifo_write_idx - voodoo->fifo_read_idx)
#define FIFO_FULL       ((voodoo->fifo_write_idx - voodoo->fifo_read_idx) >= FIFO_SIZE - 4)
#define FIFO_EMPTY      (voodoo->fifo_read_idx == voodoo->fifo_write_idx)

#define VOODOO_BUF_FRONT   0
#define VOODOO_BUF_BACK    1
#define VOODOO_BUF_AUX     2
#define VOODOO_BUF_UNKNOWN 3
#define VOODOO_BUF_COUNT   4
#define VOODOO_BUF_NONE    0xff

#define FIFO_TYPE       0xff000000
#define FIFO_ADDR       0x00ffffff

enum {
    FIFO_INVALID      = (0x00 << 24),
    FIFO_WRITEL_REG   = (0x01 << 24),
    FIFO_WRITEW_FB    = (0x02 << 24),
    FIFO_WRITEL_FB    = (0x03 << 24),
    FIFO_WRITEL_TEX   = (0x04 << 24),
    FIFO_WRITEL_2DREG = (0x05 << 24)
};

#define PARAM_SIZE       1024
#define PARAM_MASK       (PARAM_SIZE - 1)
#define PARAM_ENTRY_SIZE (1 << 31)

#define PARAM_ENTRIES(x) (voodoo->params_write_idx - voodoo->params_read_idx[x])
#define PARAM_FULL(x)    ((voodoo->params_write_idx - voodoo->params_read_idx[x]) >= PARAM_SIZE)
#define PARAM_EMPTY(x)   (voodoo->params_read_idx[x] == voodoo->params_write_idx)

typedef struct
{
    uint32_t addr_type;
    uint32_t val;
    uint8_t  target_buf;
    uint8_t  pad[3];
} fifo_entry_t;

typedef struct voodoo_params_t {
    int command;

    int32_t vertexAx;
    int32_t vertexAy;
    int32_t vertexBx;
    int32_t vertexBy;
    int32_t vertexCx;
    int32_t vertexCy;

    uint32_t startR;
    uint32_t startG;
    uint32_t startB;
    uint32_t startZ;
    uint32_t startA;

    int32_t dBdX;
    int32_t dGdX;
    int32_t dRdX;
    int32_t dAdX;
    int32_t dZdX;

    int32_t dBdY;
    int32_t dGdY;
    int32_t dRdY;
    int32_t dAdY;
    int32_t dZdY;

    int64_t startW;
    int64_t dWdX;
    int64_t dWdY;

    struct
    {
        int64_t startS;
        int64_t startT;
        int64_t startW;
        int64_t p1;
        int64_t dSdX;
        int64_t dTdX;
        int64_t dWdX;
        int64_t p2;
        int64_t dSdY;
        int64_t dTdY;
        int64_t dWdY;
        int64_t p3;
    } tmu[2];

    uint32_t color0;
    uint32_t color1;

    uint32_t fbzMode;
    uint32_t fbzColorPath;

    uint32_t    fogMode;
    rgbvoodoo_t fogColor;
    struct
    {
        uint8_t fog;
        uint8_t dfog;
    } fogTable[64];

    uint32_t alphaMode;

    uint32_t zaColor;

    int      chromaKey_r;
    int      chromaKey_g;
    int      chromaKey_b;
    uint32_t chromaKey;

    uint32_t textureMode[2];
    uint32_t tLOD[2];

    uint32_t texBaseAddr[2];
    uint32_t texBaseAddr1[2];
    uint32_t texBaseAddr2[2];
    uint32_t texBaseAddr38[2];

    uint32_t tex_base[2][LOD_MAX + 2];
    uint32_t tex_end[2][LOD_MAX + 2];
    int      tex_width[2];
    int      tex_w_mask[2][LOD_MAX + 2];
    int      tex_w_nmask[2][LOD_MAX + 2];
    int      tex_h_mask[2][LOD_MAX + 2];
    int      tex_shift[2][LOD_MAX + 2];
    int      tex_lod[2][LOD_MAX + 2];
    int      tex_entry[2];
    int      detail_max[2];
    int      detail_bias[2];
    int      detail_scale[2];

    uint32_t draw_offset;
    uint32_t aux_offset;

    int tformat[2];

    int clipLeft;
    int clipRight;
    int clipLowY;
    int clipHighY;
    int clipLeft1;
    int clipRight1;
    int clipLowY1;
    int clipHighY1;

    int sign;

    uint32_t front_offset;

    uint32_t swapbufferCMD;

    uint32_t stipple;

    int col_tiled;
    int aux_tiled;
    int row_width;
    int aux_row_width;
} voodoo_params_t;

typedef struct texture_t {
    uint32_t   base;
    uint32_t   tLOD;
    ATOMIC_INT refcount;
    ATOMIC_INT refcount_r[4];
    int        is16;
    uint32_t   palette_checksum;
    uint32_t   addr_start[4];
    uint32_t   addr_end[4];
    uint32_t  *data;
} texture_t;

typedef struct vert_t {
    float sVx;
    float sVy;
    float sRed;
    float sGreen;
    float sBlue;
    float sAlpha;
    float sVz;
    float sWb;
    float sW0;
    float sS0;
    float sT0;
    float sW1;
    float sS1;
    float sT1;
} vert_t;

typedef struct clip_t {
    int x_min;
    int x_max;
    int y_min;
    int y_max;
} clip_t;

typedef struct voodoo_t {
    mem_mapping_t mapping;

    int pci_enable;

    uint8_t  pci_slot;

    uint8_t  dac_data[8];
    int      dac_reg;
    int      dac_reg_ff;
    uint8_t  dac_readdata;
    uint16_t dac_pll_regs[16];

    float    pixel_clock;
    uint64_t line_time;

    voodoo_params_t params;

    uint32_t fbiInit0;
    uint32_t fbiInit1;
    uint32_t fbiInit2;
    uint32_t fbiInit3;
    uint32_t fbiInit4;
    uint32_t fbiInit5;
    uint32_t fbiInit6;
    uint32_t fbiInit7; /*Voodoo 2*/

    uint32_t initEnable;

    uint32_t lfbMode;

    uint32_t memBaseAddr;

    int_float fvertexAx;
    int_float fvertexAy;
    int_float fvertexBx;
    int_float fvertexBy;
    int_float fvertexCx;
    int_float fvertexCy;

    uint32_t front_offset;
    uint32_t back_offset;

    uint32_t fb_read_offset;
    uint32_t fb_write_offset;

    int row_width;
    int aux_row_width;
    int block_width;

    int col_tiled;
    int aux_tiled;

    uint8_t  *fb_mem;
    uint8_t  *tex_mem[2];
    uint16_t *tex_mem_w[2];

    int rgb_sel;

    uint32_t trexInit1[2];

    uint32_t tmuConfig;

    mutex_t *swap_mutex;
    int      swap_count;

    int        disp_buffer;
    int        draw_buffer;
    pc_timer_t timer;

    int     line;
    svga_t *svga;

    uint32_t backPorch;
    uint32_t videoDimensions;
    uint32_t hSync;
    uint32_t vSync;

    int h_total;
    int v_total;
    int v_disp;
    int h_disp;
    int v_retrace;

    struct {
        uint32_t y[4];
        uint32_t i[4];
        uint32_t q[4];
    } nccTable[2][2];

    rgba_u palette[2][256];

    rgba_u ncc_lookup[2][2][256];
    int    ncc_dirty[2];

    thread_t *fifo_thread;
    thread_t *render_thread[4];
    event_t  *wake_fifo_thread;
    event_t  *wake_main_thread;
    event_t  *fifo_not_full_event;
    event_t  *fifo_empty_event;
    ATOMIC_INT fifo_empty_signaled;
    event_t  *render_not_full_event[4];
    event_t  *wake_render_thread[4];

    int voodoo_busy;
    int render_voodoo_busy[4];

    int render_threads;
    int odd_even_mask;

    int pixel_count[4];
    int texel_count[4];
    int tri_count;
    int frame_count;
    int pixel_count_old[4];
    int texel_count_old[4];
    int wr_count;
    int rd_count;
    int tex_count;

    int      retrace_count;
    int      swap_interval;
    uint32_t swap_offset;
    int      swap_pending;

    int bilinear_enabled;
    int dithersub_enabled;

    int      fb_size;
    uint32_t fb_mask;

    int      texture_size;
    uint32_t texture_mask;

    int dual_tmus;
    int type;

    fifo_entry_t fifo[FIFO_SIZE];
    ATOMIC_INT   fifo_read_idx;
    ATOMIC_INT   fifo_write_idx;
    ATOMIC_INT   cmd_read;
    ATOMIC_INT   cmd_written;
    ATOMIC_INT   cmd_written_fifo;
    ATOMIC_INT   cmd_written_fifo_2;
    ATOMIC_INT   pending_fb_writes_buf[VOODOO_BUF_COUNT];
    ATOMIC_INT   pending_draw_cmds_buf[VOODOO_BUF_COUNT];

    voodoo_params_t params_buffer[PARAM_SIZE];
    ATOMIC_INT      params_read_idx[4];
    ATOMIC_INT      params_write_idx;

    uint32_t   cmdfifo_base;
    uint32_t   cmdfifo_end;
    uint32_t   cmdfifo_size;
    int        cmdfifo_rp;
    int        cmdfifo_ret_addr;
    int        cmdfifo_in_sub;
    int        cmdfifo_in_agp;
    ATOMIC_INT cmdfifo_depth_rd;
    ATOMIC_INT cmdfifo_depth_wr;
    ATOMIC_INT cmdfifo_enabled;
    uint32_t   cmdfifo_amin;
    uint32_t   cmdfifo_amax;
    int        cmdfifo_holecount;

    uint32_t   cmdfifo_base_2;
    uint32_t   cmdfifo_end_2;
    uint32_t   cmdfifo_size_2;
    int        cmdfifo_rp_2;
    int        cmdfifo_ret_addr_2;
    int        cmdfifo_in_sub_2;
    int        cmdfifo_in_agp_2;
    ATOMIC_INT cmdfifo_depth_rd_2;
    ATOMIC_INT cmdfifo_depth_wr_2;
    ATOMIC_INT cmdfifo_enabled_2;
    uint32_t   cmdfifo_amin_2;
    uint32_t   cmdfifo_amax_2;
    int        cmdfifo_holecount_2;

    ATOMIC_UINT cmd_status, cmd_status_2;

    uint32_t     sSetupMode;
    vert_t       verts[4];
    unsigned int vertex_ages[3];
    unsigned int vertex_next_age;
    int          num_verticies;
    int          cull_pingpong;

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
    int      bltSrcXYStride;
    int      bltDstXYStride;
    uint32_t bltSrcChromaRange;
    uint32_t bltDstChromaRange;
    int      bltSrcChromaMinR;
    int      bltSrcChromaMinG;
    int      bltSrcChromaMinB;
    int      bltSrcChromaMaxR;
    int      bltSrcChromaMaxG;
    int      bltSrcChromaMaxB;
    int      bltDstChromaMinR;
    int      bltDstChromaMinG;
    int      bltDstChromaMinB;
    int      bltDstChromaMaxR;
    int      bltDstChromaMaxG;
    int      bltDstChromaMaxB;

    int bltClipRight;
    int bltClipLeft;
    int bltClipHighY;
    int bltClipLowY;

    int      bltSrcX;
    int      bltSrcY;
    int      bltDstX;
    int      bltDstY;
    int      bltSizeX;
    int      bltSizeY;
    int      bltRop[4];
    uint16_t bltColorFg;
    uint16_t bltColorBg;

    uint32_t bltCommand;

    uint32_t leftOverlayBuf;

    struct {
        int dst_x;
        int dst_y;
        int cur_x;
        int size_x;
        int size_y;
        int x_dir;
        int y_dir;
        int dst_stride;
    } blt;

    struct {
        uint32_t bresError0;
        uint32_t bresError1;
        uint32_t clip0Min;
        uint32_t clip0Max;
        uint32_t clip1Min;
        uint32_t clip1Max;
        uint32_t colorBack;
        uint32_t colorFore;
        uint32_t command;
        uint32_t commandExtra;
        uint32_t dstBaseAddr;
        uint32_t dstFormat;
        uint32_t dstSize;
        uint32_t dstXY;
        uint32_t lineStipple;
        uint32_t lineStyle;
        uint32_t rop;
        uint32_t srcBaseAddr;
        uint32_t srcFormat;
        uint32_t srcSize;
        uint32_t srcXY;

        uint32_t colorPattern[64];

        int      bres_error_0;
        int      bres_error_1;
        uint32_t colorPattern8[64];
        uint32_t colorPattern16[64];
        uint32_t colorPattern24[64];
        int      cur_x;
        int      cur_y;
        uint32_t dstBaseAddr_tiled;
        uint32_t dstColorkeyMin;
        uint32_t dstColorkeyMax;
        int      dstSizeX;
        int      dstSizeY;
        int      dstX;
        int      dstY;
        int      dst_stride;
        int      patoff_x;
        int      patoff_y;
        uint8_t  rops[4];
        uint32_t srcBaseAddr_tiled;
        uint32_t srcColorkeyMin;
        uint32_t srcColorkeyMax;
        int      srcSizeX;
        int      srcSizeY;
        int      srcX;
        int      srcY;
        int      src_stride;
        int      old_srcX;

        /*Used for handling packed 24bpp host data*/
        int      host_data_remainder;
        uint32_t old_host_data;

        /*Polyfill coordinates*/
        int lx[2];
        int rx[2];
        int ly[2];
        int  ry[2];

        /*Polyfill state*/
        int error[2];
        int dx[2];
        int dy[2];
        int x_inc[2]; /*y_inc is always 1 for polyfill*/
        int lx_cur;
        int rx_cur;

        clip_t clip[2];

        uint8_t host_data[16384];
        int     host_data_count;
        int     host_data_size_src;
        int     host_data_size_dest;
        int     src_stride_src;
        int     src_stride_dest;

        int src_bpp;

        int line_pix_pos;
        int line_bit_pos;
        int line_rep_cnt;
        int line_bit_mask_size;
    } banshee_blt;

    struct {
        uint32_t vidOverlayStartCoords;
        uint32_t vidOverlayEndScreenCoords;
        uint32_t vidOverlayDudx;
        uint32_t vidOverlayDudxOffsetSrcWidth;
        uint32_t vidOverlayDvdy;
        uint32_t vidOverlayDvdyOffset;
#if 0
        uint32_t vidDesktopOverlayStride;
#endif

        int start_x;
        int start_y;
        int end_x;
        int end_y;
        int size_x;
        int size_y;
        int overlay_bytes;

        unsigned int src_y;
    } overlay;

    rgbvoodoo_t clutData[33];
    int         clutData_dirty;
    rgbvoodoo_t clutData256[256];
    uint32_t    video_16to32[0x10000];

    uint8_t dirty_line[2048];
    int     dirty_line_low;
    int     dirty_line_high;

    int fb_write_buffer;
    int fb_draw_buffer;
    int buffer_cutoff;
    int queued_disp_buffer;
    int queued_draw_buffer;
    int queued_fb_write_buffer;
    int queued_fb_draw_buffer;
    uint32_t queued_lfbMode;
    uint32_t queued_fbzMode;

    uint32_t tile_base;
    uint32_t tile_stride;
    int      tile_stride_shift;
    int      tile_x;
    int      tile_x_real;

    int y_origin_swap;

    int read_time;
    int write_time;
    int burst_time;

    pc_timer_t wake_timer;

    /* screen filter tables */
    uint8_t  thefilter[256][256];
    uint8_t  thefilterg[256][256];
    uint8_t  thefilterb[256][256];
    uint16_t purpleline[256][3];

    texture_t texture_cache[2][TEX_CACHE_MAX];
    uint8_t   texture_present[2][16384];
    int       texture_last_removed;

    uint32_t palette_checksum[2];
    int      palette_dirty[2];

    uint64_t time;
    int      render_time[4];
    uint64_t fifo_full_waits;
    uint64_t fifo_full_wait_ticks;
    uint64_t fifo_full_spin_checks;
    uint64_t fifo_empty_waits;
    uint64_t fifo_empty_wait_ticks;
    uint64_t fifo_empty_spin_checks;
    uint64_t render_waits;
    uint64_t render_wait_ticks;
    uint64_t render_wait_spin_checks;
    uint64_t readl_fb_count;
    uint64_t readl_fb_sync_count;
    uint64_t readl_fb_nosync_count;
    uint64_t readl_fb_relaxed_count;
    uint64_t readl_fb_sync_buf[3];
    uint64_t readl_fb_nosync_buf[3];
    uint64_t readl_fb_relaxed_buf[3];
    uint64_t readl_reg_count;
    uint64_t readl_tex_count;
    int      wait_stats_enabled;
    int      wait_stats_explicit;
    int      lfb_relax_enabled;
    int      lfb_relax_full;
    int      lfb_relax_ignore_cmdfifo;
    int      lfb_relax_ignore_draw;
    int      lfb_relax_ignore_fb_writes;
    int      lfb_relax_front_sync;

    int      force_blit_count;
    int      can_blit;
    mutex_t *force_blit_mutex;

    int   use_recompiler;
    void *codegen_data;

    struct voodoo_set_t *set;

    uint32_t launch_pending;

    uint8_t fifo_thread_run;
    uint8_t render_thread_run[4];

    uint8_t *vram;
    uint8_t *changedvram;

    void   *priv;
    uint8_t monitor_index;
} voodoo_t;

typedef struct voodoo_set_t {
    voodoo_t *voodoos[2];

    mem_mapping_t snoop_mapping;

    int nr_cards;
} voodoo_set_t;

extern rgba8_t rgb332[0x100];
extern rgba8_t ai44[0x100];
extern rgba8_t rgb565[0x10000];
extern rgba8_t argb1555[0x10000];
extern rgba8_t argb4444[0x10000];
extern rgba8_t ai88[0x10000];

void voodoo_generate_vb_filters(voodoo_t *voodoo, int fcr, int fcg);

void voodoo_recalc(voodoo_t *voodoo);
void voodoo_update_ncc(voodoo_t *voodoo, int tmu);

void *voodoo_2d3d_card_init(int type);
void  voodoo_card_close(voodoo_t *voodoo);

#endif /*VIDEO_VOODOO_COMMON_H*/
