/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Voodoo Graphics, 2, Banshee, 3 emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		leilei
 *
 *		Copyright 2008-2020 Sarah Walker.
 */

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
        VOODOO_SB50,
        VOODOO_2,
        VOODOO_BANSHEE,
        VOODOO_3
};

typedef union int_float
{
        uint32_t i;
        float f;
} int_float;

typedef struct rgbvoodoo_t
{
        uint8_t b, g, r;
        uint8_t pad;
} rgbvoodoo_t;
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
#define FIFO_FULL    ((voodoo->fifo_write_idx - voodoo->fifo_read_idx) >= FIFO_SIZE-4)
#define FIFO_EMPTY   (voodoo->fifo_read_idx == voodoo->fifo_write_idx)

#define FIFO_TYPE 0xff000000
#define FIFO_ADDR 0x00ffffff

enum
{
        FIFO_INVALID      = (0x00 << 24),
        FIFO_WRITEL_REG   = (0x01 << 24),
        FIFO_WRITEW_FB    = (0x02 << 24),
        FIFO_WRITEL_FB    = (0x03 << 24),
        FIFO_WRITEL_TEX   = (0x04 << 24),
        FIFO_WRITEL_2DREG = (0x05 << 24)
};

#define PARAM_SIZE 1024
#define PARAM_MASK (PARAM_SIZE - 1)
#define PARAM_ENTRY_SIZE (1 << 31)

#define PARAM_ENTRIES(x) (voodoo->params_write_idx - voodoo->params_read_idx[x])
#define PARAM_FULL(x)    ((voodoo->params_write_idx - voodoo->params_read_idx[x]) >= PARAM_SIZE)
#define PARAM_EMPTY(x)   (voodoo->params_read_idx[x] == voodoo->params_write_idx)

typedef struct
{
        uint32_t addr_type;
        uint32_t val;
} fifo_entry_t;

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
        rgbvoodoo_t fogColor;
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
        int clipLeft1, clipRight1, clipLowY1, clipHighY1;

        int sign;

        uint32_t front_offset;

        uint32_t swapbufferCMD;

        uint32_t stipple;

        int col_tiled, aux_tiled;
        int row_width, aux_row_width;
} voodoo_params_t;

typedef struct texture_t
{
        uint32_t base;
        uint32_t tLOD;
        volatile int refcount, refcount_r[4];
        int is16;
        uint32_t palette_checksum;
        uint32_t addr_start[4], addr_end[4];
        uint32_t *data;
} texture_t;

typedef struct vert_t
{
        float sVx, sVy;
        float sRed, sGreen, sBlue, sAlpha;
        float sVz, sWb;
        float sW0, sS0, sT0;
        float sW1, sS1, sT1;
} vert_t;

typedef struct clip_t
{
        int x_min, x_max;
        int y_min, y_max;
} clip_t;

typedef struct voodoo_t
{
        mem_mapping_t mapping;

        int pci_enable;

        uint8_t dac_data[8];
        int dac_reg, dac_reg_ff;
        uint8_t dac_readdata;
        uint16_t dac_pll_regs[16];

        float pixel_clock;
        uint64_t line_time;

        voodoo_params_t params;

        uint32_t fbiInit0, fbiInit1, fbiInit2, fbiInit3, fbiInit4;
        uint32_t fbiInit5, fbiInit6, fbiInit7; /*Voodoo 2*/

        uint32_t initEnable;

        uint32_t lfbMode;

        uint32_t memBaseAddr;

        int_float fvertexAx, fvertexAy, fvertexBx, fvertexBy, fvertexCx, fvertexCy;

        uint32_t front_offset, back_offset;

        uint32_t fb_read_offset, fb_write_offset;

        int row_width, aux_row_width;
        int block_width;
        
        int col_tiled, aux_tiled;

        uint8_t *fb_mem, *tex_mem[2];
        uint16_t *tex_mem_w[2];

        int rgb_sel;

        uint32_t trexInit1[2];

        uint32_t tmuConfig;

        mutex_t *swap_mutex;
        int swap_count;

        int disp_buffer, draw_buffer;
        pc_timer_t timer;

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
        thread_t *render_thread[4];
        event_t *wake_fifo_thread;
        event_t *wake_main_thread;
        event_t *fifo_not_full_event;
        event_t *render_not_full_event[4];
        event_t *wake_render_thread[4];

        int voodoo_busy;
        int render_voodoo_busy[4];

        int render_threads;
        int odd_even_mask;

        int pixel_count[4], texel_count[4], tri_count, frame_count;
        int pixel_count_old[4], texel_count_old[4];
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
        volatile int params_read_idx[4], params_write_idx;

        uint32_t cmdfifo_base, cmdfifo_end, cmdfifo_size;
        int cmdfifo_rp, cmdfifo_ret_addr;
        int cmdfifo_in_sub;
        volatile int cmdfifo_depth_rd, cmdfifo_depth_wr;
        volatile int cmdfifo_enabled;
        uint32_t cmdfifo_amin, cmdfifo_amax;
        int cmdfifo_holecount;

        uint32_t sSetupMode;
        vert_t verts[4];
        unsigned int vertex_ages[3];
        unsigned int vertex_next_age;
        int num_verticies;
        int cull_pingpong;

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

        uint32_t leftOverlayBuf;
        
        struct
        {
                int dst_x, dst_y;
                int cur_x;
                int size_x, size_y;
                int x_dir, y_dir;
                int dst_stride;
        } blt;

        struct
        {
                uint32_t bresError0, bresError1;
                uint32_t clip0Min, clip0Max;
                uint32_t clip1Min, clip1Max;
                uint32_t colorBack, colorFore;
                uint32_t command, commandExtra;
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

                int bres_error_0, bres_error_1;
                uint32_t colorPattern8[64], colorPattern16[64], colorPattern24[64];
                int cur_x, cur_y;
                uint32_t dstBaseAddr_tiled;
                uint32_t dstColorkeyMin, dstColorkeyMax;
                int dstSizeX, dstSizeY;
                int dstX, dstY;
                int dst_stride;
                int patoff_x, patoff_y;
                uint8_t rops[4];
                uint32_t srcBaseAddr_tiled;
                uint32_t srcColorkeyMin, srcColorkeyMax;
                int srcSizeX, srcSizeY;
                int srcX, srcY;
                int src_stride;
                int old_srcX;
                
                /*Used for handling packed 24bpp host data*/
                int host_data_remainder;
                uint32_t old_host_data;
                
                /*Polyfill coordinates*/
                int lx[2], rx[2];
                int ly[2], ry[2];

                /*Polyfill state*/
                int error[2];
                int dx[2], dy[2];
                int x_inc[2]; /*y_inc is always 1 for polyfill*/
                int lx_cur, rx_cur;

                clip_t clip[2];
                
                uint8_t host_data[16384];
                int host_data_count;
                int host_data_size_src, host_data_size_dest;
                int src_stride_src, src_stride_dest;
                
                int src_bpp;

                int line_pix_pos, line_bit_pos;
                int line_rep_cnt, line_bit_mask_size;
        } banshee_blt;
        
        struct
        {
                uint32_t vidOverlayStartCoords;
                uint32_t vidOverlayEndScreenCoords;
                uint32_t vidOverlayDudx, vidOverlayDudxOffsetSrcWidth;
                uint32_t vidOverlayDvdy, vidOverlayDvdyOffset;
                //uint32_t vidDesktopOverlayStride;
                
                int start_x, start_y;
                int end_x, end_y;
                int size_x, size_y;
                int overlay_bytes;
                
                unsigned int src_y;
        } overlay;

        rgbvoodoo_t clutData[33];
        int clutData_dirty;
        rgbvoodoo_t clutData256[256];
        uint32_t video_16to32[0x10000];

        uint8_t dirty_line[2048];
        int dirty_line_low, dirty_line_high;

        int fb_write_buffer, fb_draw_buffer;
        int buffer_cutoff;
        
        uint32_t tile_base, tile_stride;
        int tile_stride_shift, tile_x, tile_x_real;

        int y_origin_swap;

        int read_time, write_time, burst_time;

        pc_timer_t wake_timer;

        /* screen filter tables */
        uint8_t thefilter[256][256];
        uint8_t thefilterg[256][256];
        uint8_t thefilterb[256][256];
        uint16_t purpleline[256][3];

        texture_t texture_cache[2][TEX_CACHE_MAX];
        uint8_t texture_present[2][16384];
        int texture_last_removed;

        uint32_t palette_checksum[2];
        int palette_dirty[2];

        uint64_t time;
        int render_time[4];

        int force_blit_count;
        int can_blit;
        mutex_t* force_blit_mutex;

        int use_recompiler;
        void *codegen_data;

        struct voodoo_set_t *set;
        
        
        uint8_t *vram, *changedvram;
        
        void *p;
} voodoo_t;

typedef struct voodoo_set_t
{
        voodoo_t *voodoos[2];

        mem_mapping_t snoop_mapping;

        int nr_cards;
} voodoo_set_t;


extern rgba8_t rgb332[0x100], ai44[0x100], rgb565[0x10000], argb1555[0x10000], argb4444[0x10000], ai88[0x10000];


void voodoo_generate_vb_filters(voodoo_t *voodoo, int fcr, int fcg);

void voodoo_recalc(voodoo_t *voodoo);
void voodoo_update_ncc(voodoo_t *voodoo, int tmu);

void *voodoo_2d3d_card_init(int type);
void voodoo_card_close(voodoo_t *voodoo);
