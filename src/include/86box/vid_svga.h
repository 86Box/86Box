/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Generic SVGA handling.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 */
#ifndef VIDEO_SVGA_H
#    define VIDEO_SVGA_H

#    define FLAG_EXTRA_BANKS  1
#    define FLAG_ADDR_BY8     2
#    define FLAG_EXT_WRITE    4
#    define FLAG_LATCH8       8
#    define FLAG_NOSKEW       16
#    define FLAG_ADDR_BY16    32
#    define FLAG_RAMDAC_SHIFT 64
#    define FLAG_ATI          128
#    define FLAG_S3_911_16BIT 256
#    define FLAG_512K_MASK    512
#    define FLAG_NO_SHIFT3    1024 /* Needed for Bochs VBE. */
struct monitor_t;

typedef struct hwcursor_t {
    int      ena;
    int      x;
    int      y;
    int      xoff;
    int      yoff;
    int      cur_xsize;
    int      cur_ysize;
    int      v_acc;
    int      h_acc;
    uint32_t addr;
    uint32_t pitch;
} hwcursor_t;

typedef union {
    uint64_t q;
    uint32_t d[2];
    uint16_t w[4];
    uint8_t  b[8];
} latch_t;

typedef struct svga_t {
    mem_mapping_t mapping;

    uint8_t fast;
    uint8_t chain4;
    uint8_t chain2_write;
    uint8_t chain2_read;
    uint8_t ext_overscan;
    uint8_t bus_size;
    uint8_t lowres;
    uint8_t interlace;
    uint8_t linedbl;
    uint8_t rowcount;
    uint8_t set_reset_disabled;
    uint8_t bpp;
    uint8_t ramdac_type;
    uint8_t fb_only;
    uint8_t readmode;
    uint8_t writemode;
    uint8_t readplane;
    uint8_t hwcursor_oddeven;
    uint8_t dac_hwcursor_oddeven;
    uint8_t overlay_oddeven;
    uint8_t fcr;
    uint8_t hblank_overscan;
    uint8_t vidsys_ena;
    uint8_t sleep;

    int dac_addr;
    int dac_pos;
    int dac_r;
    int dac_g;
    int dac_b;
    int vtotal;
    int dispend;
    int vdisp;
    int vsyncstart;
    int split;
    int vblankstart;
    int hdisp;
    int hdisp_old;
    int htotal;
    int hdisp_time;
    int rowoffset;
    int dispon;
    int hdisp_on;
    int vc;
    int sc;
    int linepos;
    int vslines;
    int linecountff;
    int oddeven;
    int con;
    int cursoron;
    int blink;
    int scrollcache;
    int char_width;
    int firstline;
    int lastline;
    int firstline_draw;
    int lastline_draw;
    int displine;
    int fullchange;
    int x_add;
    int y_add;
    int pan;
    int vram_display_mask;
    int vidclock;
    int dots_per_clock;
    int hwcursor_on;
    int dac_hwcursor_on;
    int overlay_on;
    int set_override;
    int hblankstart;
    int hblankend;
    int hblank_end_val;
    int hblank_end_len;
    int hblank_end_mask;
    int hblank_sub;
    int packed_4bpp;
    int ps_bit_bug;
    int ati_4color;

    /*The three variables below allow us to implement memory maps like that seen on a 1MB Trio64 :
      0MB-1MB - VRAM
      1MB-2MB - VRAM mirror
      2MB-4MB - open bus
      4MB-xMB - mirror of above

      For the example memory map, decode_mask would be 4MB-1 (4MB address space), vram_max would be 2MB
      (present video memory only responds to first 2MB), vram_mask would be 1MB-1 (video memory wraps at 1MB)
    */
    uint32_t  decode_mask;
    uint32_t  vram_max;
    uint32_t  vram_mask;
    uint32_t  charseta;
    uint32_t  charsetb;
    uint32_t  adv_flags;
    uint32_t  ma_latch;
    uint32_t  ca_adj;
    uint32_t  ma;
    uint32_t  maback;
    uint32_t  write_bank;
    uint32_t  read_bank;
    uint32_t  extra_banks[2];
    uint32_t  banked_mask;
    uint32_t  ca;
    uint32_t  overscan_color;
    uint32_t *map8;
    uint32_t  pallook[512];

    PALETTE vgapal;

    uint64_t dispontime;
    uint64_t dispofftime;
    latch_t  latch;

    pc_timer_t timer;
    pc_timer_t timer8514;
    pc_timer_t timer_xga;

    double clock;
    double clock8514;
    double clock_xga;

    double multiplier;

    hwcursor_t hwcursor;
    hwcursor_t hwcursor_latch;
    hwcursor_t dac_hwcursor;
    hwcursor_t dac_hwcursor_latch;
    hwcursor_t overlay;
    hwcursor_t overlay_latch;

    void (*render)(struct svga_t *svga);
    void (*render8514)(struct svga_t *svga);
    void (*render_xga)(struct svga_t *svga);
    void (*recalctimings_ex)(struct svga_t *svga);

    void (*video_out)(uint16_t addr, uint8_t val, void *priv);
    uint8_t (*video_in)(uint16_t addr, void *priv);

    void (*hwcursor_draw)(struct svga_t *svga, int displine);

    void (*dac_hwcursor_draw)(struct svga_t *svga, int displine);

    void (*overlay_draw)(struct svga_t *svga, int displine);

    void (*vblank_start)(struct svga_t *svga);

    void (*write)(uint32_t addr, uint8_t val, void *priv);
    void (*writew)(uint32_t addr, uint16_t val, void *priv);
    void (*writel)(uint32_t addr, uint32_t val, void *priv);

    uint8_t (*read)(uint32_t addr, void *priv);
    uint16_t (*readw)(uint32_t addr, void *priv);
    uint32_t (*readl)(uint32_t addr, void *priv);

    void (*ven_write)(struct svga_t *svga, uint8_t val, uint32_t addr);
    float (*getclock)(int clock, void *priv);
    float (*getclock8514)(int clock, void *priv);

    /* Called when VC=R18 and friends. If this returns zero then MA resetting
       is skipped. Matrox Mystique in Power mode reuses this counter for
       vertical line interrupt*/
    int (*line_compare)(struct svga_t *svga);

    /*Called at the start of vertical sync*/
    void (*vsync_callback)(struct svga_t *svga);

    uint32_t (*translate_address)(uint32_t addr, void *priv);
    /*If set then another device is driving the monitor output and the SVGA
      card should not attempt to display anything */
    int   override;
    void *priv;

    uint8_t  crtc[256];
    uint8_t  gdcreg[256];
    uint8_t  attrregs[32];
    uint8_t  seqregs[256];
    uint8_t  egapal[16];
    uint8_t *vram;
    uint8_t *changedvram;

    uint8_t crtcreg;
    uint8_t gdcaddr;
    uint8_t attrff;
    uint8_t attr_palette_enable;
    uint8_t attraddr;
    uint8_t seqaddr;
    uint8_t miscout;
    uint8_t cgastat;
    uint8_t scrblank;
    uint8_t plane_mask;
    uint8_t writemask;
    uint8_t colourcompare;
    uint8_t colournocare;
    uint8_t dac_mask;
    uint8_t dac_status;
    uint8_t dpms;
    uint8_t dpms_ui;
    uint8_t color_2bpp;
    uint8_t ksc5601_sbyte_mask;
    uint8_t ksc5601_udc_area_msb[2];

    int      ksc5601_swap_mode;
    uint16_t ksc5601_english_font_type;

    int vertical_linedbl;

    /*Used to implement CRTC[0x17] bit 2 hsync divisor*/
    int hsync_divisor;

    /*Tseng-style chain4 mode - CRTC dword mode is the same as byte mode, chain4
      addresses are shifted to match*/
    int packed_chain4;

    /*Disable 8bpp blink mode - some cards support it, some don't, it's a weird mode
      If mode 13h appears in a reddish-brown background (0x88) with dark green text (0x8F),
      you should set this flag when entering that mode*/
    int disable_blink;

    /*Force CRTC to dword mode, regardless of CR14/CR17. Required for S3 enhanced mode*/
    int force_dword_mode;

    int force_old_addr;

    int remap_required;
    uint32_t (*remap_func)(struct svga_t *svga, uint32_t in_addr);

    void *ramdac;
    void *clock_gen;

    /* Monitor Index */
    uint8_t monitor_index;

    /* Pointer to monitor */
    monitor_t *monitor;

    /* Enable LUT mapping of >= 24 bpp modes. */
    int lut_map;

    /* Override the horizontal blanking stuff. */
    int hoverride;

    /* Return a 32 bpp color from a 15/16 bpp color. */
    uint32_t (*conv_16to32)(struct svga_t *svga, uint16_t color, uint8_t bpp);

    void *  dev8514;
    void *  ext8514;
    void *  clock_gen8514;
    void *  xga;
} svga_t;

extern void     ibm8514_set_poll(svga_t *svga);
extern void     ibm8514_poll(void *priv);
extern void     ibm8514_recalctimings(svga_t *svga);
extern uint8_t  ibm8514_ramdac_in(uint16_t port, void *priv);
extern void     ibm8514_ramdac_out(uint16_t port, uint8_t val, void *priv);
extern void     ibm8514_accel_out_fifo(svga_t *svga, uint16_t port, uint32_t val, int len);
extern void     ibm8514_accel_out(uint16_t port, uint32_t val, svga_t *svga, int len);
extern uint16_t ibm8514_accel_in_fifo(svga_t *svga, uint16_t port, int len);
extern uint8_t  ibm8514_accel_in(uint16_t port, svga_t *svga);
extern int      ibm8514_cpu_src(svga_t *svga);
extern int      ibm8514_cpu_dest(svga_t *svga);
extern void     ibm8514_accel_out_pixtrans(svga_t *svga, uint16_t port, uint32_t val, int len);
extern void     ibm8514_short_stroke_start(int count, int cpu_input, uint32_t mix_dat, uint32_t cpu_dat, svga_t *svga, uint8_t ssv, int len);
extern void     ibm8514_accel_start(int count, int cpu_input, uint32_t mix_dat, uint32_t cpu_dat, svga_t *svga, int len);

extern void     ati8514_out(uint16_t addr, uint8_t val, void *priv);
extern uint8_t  ati8514_in(uint16_t addr, void *priv);
extern void     ati8514_recalctimings(svga_t *svga);
extern uint8_t  ati8514_mca_read(int port, void *priv);
extern void     ati8514_mca_write(int port, uint8_t val, void *priv);
extern void     ati8514_pos_write(uint16_t port, uint8_t val, void *priv);
extern void     ati8514_init(svga_t *svga, void *ext8514, void *dev8514);

extern void     xga_write_test(uint32_t addr, uint8_t val, void *priv);
extern uint8_t  xga_read_test(uint32_t addr, void *priv);
extern void     xga_set_poll(svga_t *svga);
extern void     xga_poll(void *priv);
extern void     xga_recalctimings(svga_t *svga);

extern uint32_t svga_decode_addr(svga_t *svga, uint32_t addr, int write);

extern int  svga_init(const device_t *info, svga_t *svga, void *priv, int memsize,
                      void (*recalctimings_ex)(struct svga_t *svga),
                      uint8_t (*video_in)(uint16_t addr, void *priv),
                      void (*video_out)(uint16_t addr, uint8_t val, void *priv),
                      void (*hwcursor_draw)(struct svga_t *svga, int displine),
                      void (*overlay_draw)(struct svga_t *svga, int displine));
extern void svga_recalctimings(svga_t *svga);
extern void svga_close(svga_t *svga);

uint8_t  svga_read(uint32_t addr, void *priv);
uint16_t svga_readw(uint32_t addr, void *priv);
uint32_t svga_readl(uint32_t addr, void *priv);
void     svga_write(uint32_t addr, uint8_t val, void *priv);
void     svga_writew(uint32_t addr, uint16_t val, void *priv);
void     svga_writel(uint32_t addr, uint32_t val, void *priv);
uint8_t  svga_read_linear(uint32_t addr, void *priv);
uint8_t  svga_readb_linear(uint32_t addr, void *priv);
uint16_t svga_readw_linear(uint32_t addr, void *priv);
uint32_t svga_readl_linear(uint32_t addr, void *priv);
void     svga_write_linear(uint32_t addr, uint8_t val, void *priv);
void     svga_writeb_linear(uint32_t addr, uint8_t val, void *priv);
void     svga_writew_linear(uint32_t addr, uint16_t val, void *priv);
void     svga_writel_linear(uint32_t addr, uint32_t val, void *priv);

void svga_add_status_info(char *s, int max_len, void *priv);

extern uint8_t svga_rotate[8][256];

void    svga_out(uint16_t addr, uint8_t val, void *priv);
uint8_t svga_in(uint16_t addr, void *priv);

svga_t *svga_get_pri(void);
void    svga_set_override(svga_t *svga, int val);

void svga_set_ramdac_type(svga_t *svga, int type);
void svga_close(svga_t *svga);

uint32_t svga_mask_addr(uint32_t addr, svga_t *svga);
uint32_t svga_mask_changedaddr(uint32_t addr, svga_t *svga);

void svga_doblit(int wx, int wy, svga_t *svga);
void svga_set_poll(svga_t *svga);
void svga_poll(void *priv);

enum {
    RAMDAC_6BIT = 0,
    RAMDAC_8BIT
};

uint32_t svga_lookup_lut_ram(svga_t* svga, uint32_t val);

/* We need a way to add a device with a pointer to a parent device so it can attach itself to it, and
   possibly also a second ATi 68860 RAM DAC type that auto-sets SVGA render on RAM DAC render change. */
extern void    ati68860_ramdac_out(uint16_t addr, uint8_t val, void *priv, svga_t *svga);
extern uint8_t ati68860_ramdac_in(uint16_t addr, void *priv, svga_t *svga);
extern void    ati68860_set_ramdac_type(void *priv, int type);
extern void    ati68860_ramdac_set_render(void *priv, svga_t *svga);
extern void    ati68860_ramdac_set_pallook(void *priv, int i, uint32_t col);
extern void    ati68860_hwcursor_draw(svga_t *svga, int displine);

extern void    ati68875_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, void *priv, svga_t *svga);
extern uint8_t ati68875_ramdac_in(uint16_t addr, int rs2, int rs3, void *priv, svga_t *svga);

extern void    att49x_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga);
extern uint8_t att49x_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga);

extern void    att498_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga);
extern uint8_t att498_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga);
extern float   av9194_getclock(int clock, void *priv);

extern void    bt481_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga);
extern uint8_t bt481_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga);

extern void    bt48x_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, void *priv, svga_t *svga);
extern uint8_t bt48x_ramdac_in(uint16_t addr, int rs2, int rs3, void *priv, svga_t *svga);
extern void    bt48x_recalctimings(void *priv, svga_t *svga);
extern void    bt48x_hwcursor_draw(svga_t *svga, int displine);

extern void    ibm_rgb528_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga);
extern uint8_t ibm_rgb528_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga);
extern void    ibm_rgb528_recalctimings(void *priv, svga_t *svga);
extern void    ibm_rgb528_hwcursor_draw(svga_t *svga, int displine);

extern void  icd2061_write(void *priv, int val);
extern float icd2061_getclock(int clock, void *priv);

/* The code is the same, the #define's are so that the correct name can be used. */
#    define ics9161_write    icd2061_write
#    define ics9161_getclock icd2061_getclock

extern float ics2494_getclock(int clock, void *priv);

extern void   ics2595_write(void *priv, int strobe, int dat);
extern double ics2595_getclock(void *priv);
extern void   ics2595_setclock(void *priv, double clock);

extern void    sc1148x_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga);
extern uint8_t sc1148x_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga);

extern void    sc1502x_ramdac_out(uint16_t addr, uint8_t val, void *priv, svga_t *svga);
extern uint8_t sc1502x_ramdac_in(uint16_t addr, void *priv, svga_t *svga);

extern void    sdac_ramdac_out(uint16_t addr, int rs2, uint8_t val, void *priv, svga_t *svga);
extern uint8_t sdac_ramdac_in(uint16_t addr, int rs2, void *priv, svga_t *svga);
extern float   sdac_getclock(int clock, void *priv);

extern void    stg_ramdac_out(uint16_t addr, uint8_t val, void *priv, svga_t *svga);
extern uint8_t stg_ramdac_in(uint16_t addr, void *priv, svga_t *svga);
extern float   stg_getclock(int clock, void *priv);

extern void    tkd8001_ramdac_out(uint16_t addr, uint8_t val, void *priv, svga_t *svga);
extern uint8_t tkd8001_ramdac_in(uint16_t addr, void *priv, svga_t *svga);

extern void     tvp3026_ramdac_out(uint16_t addr, int rs2, int rs3, uint8_t val, void *priv, svga_t *svga);
extern uint8_t  tvp3026_ramdac_in(uint16_t addr, int rs2, int rs3, void *priv, svga_t *svga);
extern uint32_t tvp3026_conv_16to32(svga_t* svga, uint16_t color, uint8_t bpp);
extern void     tvp3026_recalctimings(void *priv, svga_t *svga);
extern void     tvp3026_hwcursor_draw(svga_t *svga, int displine);
extern float    tvp3026_getclock(int clock, void *priv);
extern void     tvp3026_gpio(uint8_t (*read)(uint8_t cntl, void *priv), void (*write)(uint8_t cntl, uint8_t data, void *priv), void *cb_priv, void *priv);

#    ifdef EMU_DEVICE_H
extern const device_t ati68860_ramdac_device;
extern const device_t ati68875_ramdac_device;
extern const device_t att490_ramdac_device;
extern const device_t att491_ramdac_device;
extern const device_t att492_ramdac_device;
extern const device_t att498_ramdac_device;
extern const device_t av9194_device;
extern const device_t bt481_ramdac_device;
extern const device_t bt484_ramdac_device;
extern const device_t att20c504_ramdac_device;
extern const device_t bt485_ramdac_device;
extern const device_t att20c505_ramdac_device;
extern const device_t bt485a_ramdac_device;
extern const device_t gendac_ramdac_device;
extern const device_t ibm_rgb528_ramdac_device;
extern const device_t ics2494an_305_device;
extern const device_t ati18810_device;
extern const device_t ati18811_0_device;
extern const device_t ati18811_1_device;
extern const device_t ics2595_device;
extern const device_t icd2061_device;
extern const device_t ics9161_device;
extern const device_t sc11483_ramdac_device;
extern const device_t sc11487_ramdac_device;
extern const device_t sc11486_ramdac_device;
extern const device_t sc11484_nors2_ramdac_device;
extern const device_t sc1502x_ramdac_device;
extern const device_t sdac_ramdac_device;
extern const device_t stg_ramdac_device;
extern const device_t tkd8001_ramdac_device;
extern const device_t tseng_ics5301_ramdac_device;
extern const device_t tseng_ics5341_ramdac_device;
extern const device_t tvp3026_ramdac_device;
#    endif

#endif /*VIDEO_SVGA_H*/
