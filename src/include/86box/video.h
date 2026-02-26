/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the video controller module.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 */
#ifndef EMU_VIDEO_H
#define EMU_VIDEO_H

#ifdef __cplusplus
#    include <atomic>
using atomic_bool = std::atomic_bool;
using atomic_int  = std::atomic_int;
#else
#    include <stdatomic.h>
#endif

#define makecol(r, g, b)   ((b) | ((g) << 8) | ((r) << 16) | 0xff000000)
#define makecol32(r, g, b) ((b) | ((g) << 8) | ((r) << 16) | 0xff000000)
#define getcolr(color) (((color) >> 16) & 0xFF)
#define getcolg(color) (((color) >> 8) & 0xFF)
#define getcolb(color) ((color) & 0xFF)

#ifdef __cplusplus
extern "C" {
#endif

enum {
    VID_NONE = 0,
    VID_INTERNAL
};

enum {
    FULLSCR_SCALE_FULL = 0,
    FULLSCR_SCALE_43,
    FULLSCR_SCALE_KEEPRATIO,
    FULLSCR_SCALE_INT,
    FULLSCR_SCALE_INT43
};

enum {
    VIDEO_ISA = 0,
    VIDEO_MCA,
    VIDEO_BUS,
    VIDEO_PCI,
    VIDEO_AGP
};

typedef enum video_font_format_e 
{
    FONT_FORMAT_MDA = 0,
    FONT_FORMAT_PC200 = 1,
    FONT_FORMAT_CGA = 2,
    FONT_FORMAT_WY700 = 3,
    FONT_FORMAT_MDSI_GENIUS = 4,
    FONT_FORMAT_TOSHIBA_3100E = 5,
    FONT_FORMAT_KSC6501 = 6,
    FONT_FORMAT_SIGMA = 7,
    FONT_FORMAT_PC1512_T1000 = 8,
    FONT_FORMAT_IM1024 = 9,
    FONT_FORMAT_PRAVETZ = 10,
} video_font_format;

#define VIDEO_FLAG_TYPE_CGA     0
#define VIDEO_FLAG_TYPE_MDA     1
#define VIDEO_FLAG_TYPE_SPECIAL 2
#define VIDEO_FLAG_TYPE_8514    3
#define VIDEO_FLAG_TYPE_XGA     4
#define VIDEO_FLAG_TYPE_NONE    5
#define VIDEO_FLAG_TYPE_MASK    7

#define VIDEO_FLAG_TYPE_SECONDARY VIDEO_FLAG_TYPE_SPECIAL

#define FONT_IBM_MDA_437_PATH        "roms/video/mda/mda.rom"
#define FONT_IBM_MDA_437_NORDIC_PATH "roms/video/mda/4733197.bin"
#define FONT_KAM_PATH                "roms/video/mda/kam.bin"
#define FONT_KAMCL16_PATH            "roms/video/mda/kamcl16.bin"
#define FONT_TULIP_DGA_PATH          "roms/video/mda/tulip-dga-bios.bin"

typedef struct video_timings_t {
    int type;
    int write_b;
    int write_w;
    int write_l;
    int read_b;
    int read_w;
    int read_l;
} video_timings_t;

typedef struct bitmap_t {
    int       w;
    int       h;
    uint32_t *dat;
    uint32_t *line[2112];
} bitmap_t;

typedef struct rgb_t {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

typedef struct dbcs_font_t {
    uint8_t chr[32];
} dbcs_font_t;

struct blit_data_struct;

typedef struct monitor_t {
    char                     name[512];
    int                      mon_xsize;
    int                      mon_ysize;
    int                      mon_scrnsz_x;
    int                      mon_scrnsz_y;
    int                      mon_efscrnsz_y;
    int                      mon_unscaled_size_x;
    int                      mon_unscaled_size_y;
    double                   mon_res_x;
    double                   mon_res_y;
    int                      mon_bpp;
    bitmap_t                *target_buffer;
    int                      mon_video_timing_read_b;
    int                      mon_video_timing_read_w;
    int                      mon_video_timing_read_l;
    int                      mon_video_timing_write_b;
    int                      mon_video_timing_write_w;
    int                      mon_video_timing_write_l;
    int                      mon_overscan_x;
    int                      mon_overscan_y;
    int                      mon_force_resize;
    int                      mon_fullchange;
    int                      mon_changeframecount;
    int                      mon_renderedframes;
    atomic_int               mon_actualrenderedframes;
    atomic_int               mon_screenshots;
    atomic_int               mon_screenshots_clipboard;
    atomic_int               mon_screenshots_raw;
    atomic_int               mon_screenshots_raw_clipboard;
    uint32_t                *mon_pal_lookup;
    int                     *mon_cga_palette;
    int                      mon_pal_lookup_static;  /* Whether it should not be freed by the API. */
    int                      mon_cga_palette_static; /* Whether it should not be freed by the API. */
    const video_timings_t   *mon_vid_timings;
    int                      mon_vid_type;
    atomic_bool              mon_interlace;
    atomic_bool              mon_composite;
    struct blit_data_struct *mon_blit_data_ptr;
} monitor_t;

typedef struct monitor_settings_t {
    int mon_window_x; /* (C) window size and position info. */
    int mon_window_y;
    int mon_window_w;
    int mon_window_h;
    int mon_window_maximized;
} monitor_settings_t;

#define MONITORS_NUM 2
extern monitor_t          monitors[MONITORS_NUM];
extern monitor_settings_t monitor_settings[MONITORS_NUM];
extern atomic_bool        doresize_monitors[MONITORS_NUM];
extern int                monitor_index_global;
extern int                show_second_monitors;
extern int                video_fullscreen_scale_maximized;

typedef rgb_t PALETTE[256];

#if 0
extern int changeframecount;
#endif

extern volatile int screenshots;
#if 0
extern bitmap_t *buffer32;
#endif
#define buffer32             (monitors[monitor_index_global].target_buffer)
#define pal_lookup           (monitors[monitor_index_global].mon_pal_lookup)
#define overscan_x           (monitors[monitor_index_global].mon_overscan_x)
#define overscan_y           (monitors[monitor_index_global].mon_overscan_y)
#define video_timing_read_b  (monitors[monitor_index_global].mon_video_timing_read_b)
#define video_timing_read_l  (monitors[monitor_index_global].mon_video_timing_read_l)
#define video_timing_read_w  (monitors[monitor_index_global].mon_video_timing_read_w)
#define video_timing_write_b (monitors[monitor_index_global].mon_video_timing_write_b)
#define video_timing_write_l (monitors[monitor_index_global].mon_video_timing_write_l)
#define video_timing_write_w (monitors[monitor_index_global].mon_video_timing_write_w)
#define video_res_x          (monitors[monitor_index_global].mon_res_x)
#define video_res_y          (monitors[monitor_index_global].mon_res_y)
#define video_bpp            (monitors[monitor_index_global].mon_bpp)
#define xsize                (monitors[monitor_index_global].mon_xsize)
#define ysize                (monitors[monitor_index_global].mon_ysize)
#define cga_palette          (*monitors[monitor_index_global].mon_cga_palette)
#define changeframecount     (monitors[monitor_index_global].mon_changeframecount)
#define scrnsz_x             (monitors[monitor_index_global].mon_scrnsz_x)
#define scrnsz_y             (monitors[monitor_index_global].mon_scrnsz_y)
#define efscrnsz_y           (monitors[monitor_index_global].mon_efscrnsz_y)
#define unscaled_size_x      (monitors[monitor_index_global].mon_unscaled_size_x)
#define unscaled_size_y      (monitors[monitor_index_global].mon_unscaled_size_y)

#define CGAPAL_CGA_START    16            // Where the 16-color cga text/composite starts


extern PALETTE      cgapal;
extern PALETTE      cgapal_mono[6];
#if 0
extern uint32_t     pal_lookup[256];
#endif
extern int          video_fullscreen;
extern int          video_fullscreen_scale;
extern uint8_t      fontdat[2048][8];      /* IBM CGA font */
extern uint8_t      fontdatm[2048][16];    /* IBM MDA font */
extern uint8_t      fontdatw[512][32];     /* Wyse700 font */
extern uint8_t      fontdat8x12[256][16];  /* MDSI Genius font */
extern uint8_t      fontdat12x18[256][36]; /* IM1024 font */
extern dbcs_font_t *fontdatksc5601;        /* Korean KSC-5601 font */
extern dbcs_font_t *fontdatksc5601_user;   /* Korean KSC-5601 user defined font */
extern uint32_t    *video_6to8;
extern uint32_t    *video_8togs;
extern uint32_t    *video_8to32;
extern uint32_t    *video_15to32;
extern uint32_t    *video_16to32;
extern int          enable_overscan;
extern int          force_43;
extern int          vid_resize;
extern int          herc_blend;
extern int          vid_cga_contrast;
extern int          video_grayscale;
extern int          video_graytype;

extern double cpuclock;
extern int    emu_fps;
extern int    frames;
extern int    readflash;
extern int    ibm8514_active;
extern int    xga_active;

/* Function handler pointers. */
extern void (*video_recalctimings)(void);
extern void video_screenshot_monitor(uint32_t *buf, int start_x, int start_y, int row_len, int monitor_index);
extern void video_screenshot(uint32_t *buf, int start_x, int start_y, int row_len);

#ifdef _WIN32
extern void * (__cdecl *video_copy)(void *_Dst, const void *_Src, size_t _Size);
extern void *__cdecl video_transform_copy(void *_Dst, const void *_Src, size_t _Size);
#else
extern void *(*video_copy)(void *__restrict _Dst, const void *__restrict _Src, size_t _Size);
extern void *video_transform_copy(void *__restrict _Dst, const void *__restrict _Src, size_t _Size);
#endif

/* Table functions. */
extern int video_card_available(int card);
#ifdef EMU_DEVICE_H
extern const device_t *video_card_getdevice(int card);
#endif
extern int         video_card_has_config(int card);
extern const char *video_get_internal_name(int card);
extern int         video_get_video_from_internal_name(char *s);
extern int         video_card_get_flags(int card);
extern int         video_is_mda(void);
extern int         video_is_cga(void);
extern void        video_inform_monitor(int type, const video_timings_t *ptr, int monitor_index);
extern int         video_get_type_monitor(int monitor_index);

extern void video_setblit(void (*blit)(int, int, int, int, int));
extern void video_blend(int x, int y);
extern void video_blit_memtoscreen_8(int x, int y, int w, int h);
extern void video_blend_monitor(int x, int y, int monitor_index);
extern void video_process_8_monitor(int x, int y, int monitor_index);
extern void video_blit_memtoscreen_monitor(int x, int y, int w, int h, int monitor_index);
extern void video_blit_complete_monitor(int monitor_index);
extern void video_wait_for_blit_monitor(int monitor_index);
extern void video_wait_for_buffer_monitor(int monitor_index);

extern bitmap_t *create_bitmap(int w, int h);
extern void      destroy_bitmap(bitmap_t *b);
extern void      cgapal_rebuild_monitor(int monitor_index);
extern void      hline(bitmap_t *b, int x1, int y, int x2, uint32_t col);
extern void      updatewindowsize(int x, int y);

extern void    video_monitor_init(int);
extern void    video_monitor_close(int);
extern void    video_init(void);
extern void    video_close(void);
extern void    video_reset_close(void);
extern void    video_pre_reset(int card);
extern void    video_reset(int card);
extern void    video_post_reset(void);
extern void    video_voodoo_init(void);
extern uint8_t video_force_resize_get_monitor(int monitor_index);
extern void    video_force_resize_set_monitor(uint8_t res, int monitor_index);
extern void    video_update_timing(void);

#define LOAD_FONT_NO_OFFSET       0
extern void     video_load_font(char *fn, int format, int offset);
extern uint32_t video_color_transform(uint32_t color);

#define video_inform(type, video_timings_ptr) video_inform_monitor(type, video_timings_ptr, monitor_index_global)
#define video_get_type()                      video_get_type_monitor(0)
#define video_blend(x, y)                     video_blend_monitor(x, y, monitor_index_global)
#define video_blit_memtoscreen(x, y, w, h)    video_blit_memtoscreen_monitor(x, y, w, h, monitor_index_global)
#define video_process_8(x, y)                 video_process_8_monitor(x, y, monitor_index_global)
#define video_blit_complete()                 video_blit_complete_monitor(monitor_index_global)
#define video_wait_for_blit()                 video_wait_for_blit_monitor(monitor_index_global)
#define video_wait_for_buffer()               video_wait_for_buffer_monitor(monitor_index_global)
#define cgapal_rebuild()                      cgapal_rebuild_monitor(monitor_index_global)
#define video_force_resize_get()              video_force_resize_get_monitor(monitor_index_global)
#define video_force_resize_set(val)           video_force_resize_set_monitor(val, monitor_index_global)

#ifdef __cplusplus
}
#endif

#ifdef EMU_DEVICE_H
/* IBM XGA */
extern void xga_device_add(void);

/* IBM 8514/A and clones*/
extern void ibm8514_device_add(void);
extern const device_t mach8_vga_isa_device;
extern const device_t mach32_isa_device;
extern const device_t mach32_vlb_device;
extern const device_t mach32_mca_device;
extern const device_t mach32_pci_device;
extern const device_t mach32_onboard_pci_device;

/* IBM Display Adapter (PS/55) */
extern void da2_device_add(void);

/* ATi Mach64 */
extern const device_t mach64gx_isa_device;
extern const device_t mach64gx_vlb_device;
extern const device_t mach64gx_pci_device;
extern const device_t mach64ct_device;
extern const device_t mach64ct_device_onboard;
extern const device_t mach64vt_device;
extern const device_t mach64vt2_device;
extern const device_t mach64vt3_onboard_device;

/* ATi 18800 */
extern const device_t ati18800_wonder_device;
extern const device_t ati18800_vga88_device;
extern const device_t ati18800_device;

/* ATi 28800 */
extern const device_t ati28800_device;
extern const device_t ati28800_wonder1024d_xl_plus_device;
extern const device_t ati28800k_device;
extern const device_t ati28800k_spc4620p_device;
extern const device_t ati28800k_spc6033p_device;
extern const device_t compaq_ati28800_device;
#    ifdef USE_XL24
extern const device_t ati28800_wonderxl24_device;
#    endif /* USE_XL24 */

/* Bochs */
extern const device_t bochs_svga_device;

/* Chips & Technologies */
extern const device_t chips_69000_device;
extern const device_t chips_69000_onboard_device;

/* Cirrus Logic GD54xx */
extern const device_t gd5401_isa_device;
extern const device_t gd5401_onboard_device;
extern const device_t gd5402_isa_device;
extern const device_t gd5402_onboard_device;
extern const device_t gd5402_onboard_commodore_device;
extern const device_t gd5420_isa_device;
extern const device_t gd5420_onboard_device;
extern const device_t gd5422_isa_device;
extern const device_t gd5424_vlb_device;
extern const device_t gd5424_onboard_device;
extern const device_t gd5426_isa_device;
extern const device_t gd5426_diamond_speedstar_pro_a1_isa_device;
extern const device_t gd5426_vlb_device;
extern const device_t gd5426_onboard_isa_device;
extern const device_t gd5426_onboard_device;
extern const device_t gd5428_isa_device;
extern const device_t gd5428_vlb_onboard_device;
extern const device_t gd5428_vlb_onboard_tandy_device;
extern const device_t gd5428_vlb_device;
extern const device_t gd5428_diamond_speedstar_pro_b1_vlb_device;
extern const device_t gd5428_boca_isa_device;
extern const device_t gd5428_mca_device;
extern const device_t gd5426_mca_device;
extern const device_t gd5428_onboard_device;
extern const device_t gd5428_onboard_vlb_device;
extern const device_t gd5429_isa_device;
extern const device_t gd5429_vlb_device;
extern const device_t gd5430_diamond_speedstar_pro_se_a8_vlb_device;
extern const device_t gd5430_vlb_device;
extern const device_t gd5430_onboard_vlb_device;
extern const device_t gd5430_pci_device;
extern const device_t gd5430_onboard_pci_device;
extern const device_t gd5434_isa_device;
extern const device_t gd5434_diamond_speedstar_64_a3_isa_device;
extern const device_t gd5434_onboard_pci_device;
extern const device_t gd5434_vlb_device;
extern const device_t gd5434_pci_device;
extern const device_t gd5436_pci_device;
extern const device_t gd5436_onboard_pci_device;
extern const device_t gd5440_pci_device;
extern const device_t gd5440_onboard_pci_device;
extern const device_t gd5446_pci_device;
extern const device_t gd5446_stb_pci_device;
extern const device_t gd5480_pci_device;

/* IBM CGA */
extern const device_t cga_device;

/* Pravetz CGA */
extern const device_t cga_pravetz_device;

/* Compaq CGA */
extern const device_t compaq_cga_device;
extern const device_t compaq_cga_2_device;
extern const device_t compaq_plasma_device;

/* Olivetti OGC */
extern const device_t ogc_device;
extern const device_t ogc_m24_device;

/* Chips & Technologies 82C425 */
extern const device_t f82c425_video_device;

/* NCR NGA */
extern const device_t nga_device;

/* Tseng ET3000AX */
extern const device_t et3000_isa_device;

/* Tseng ET4000AX */
extern const device_t et4000_tc6058af_isa_device;
extern const device_t et4000_isa_device;
extern const device_t et4000k_isa_device;
extern const device_t et4000k_tg286_isa_device;
extern const device_t et4000_kasan_isa_device;
extern const device_t et4000_mca_device;

/* Tseng ET4000-W32x */
extern const device_t et4000w32_machspeed_vga_gui_2400s_isa_device;
extern const device_t et4000w32_machspeed_vga_gui_2400s_vlb_device;
extern const device_t et4000w32_onboard_device;
extern const device_t et4000w32i_axis_microdevice_isa_device;
extern const device_t et4000w32i_hercules_dynamite_pro_vlb_device;
extern const device_t et4000w32p_videomagic_revb_vlb_device;
extern const device_t et4000w32p_cardex_revc_vlb_device;
extern const device_t et4000w32p_cardex_revc_pci_device;
extern const device_t et4000w32p_cardex_revd_vlb_device;
extern const device_t et4000w32p_cardex_revd_pci_device;
extern const device_t et4000w32p_diamond_revd_vlb_device;
extern const device_t et4000w32p_diamond_revd_pci_device;
extern const device_t et4000w32p_generic_revd_vlb_device;
extern const device_t et4000w32p_generic_revd_pci_device;

/* MDSI Genius VHR */
extern const device_t genius_device;

/* Hercules */
extern const device_t hercules_device;
extern const device_t herculesplus_device;
extern const device_t incolor_device;

/* Headland GC-2xx/HT-2xx */
extern const device_t g2_gc205_device;
extern const device_t v7_vga_1024i_device;
extern const device_t radius_svga_multiview_isa_device;
extern const device_t radius_svga_multiview_mca_device;
extern const device_t ht216_32_pb410a_device;
extern const device_t ht216_32_standalone_device;

/* Professional Graphics Controller */
extern const device_t im1024_device;
extern const device_t pgc_device;

/* Matrox MGA */
extern const device_t millennium_device;
extern const device_t mystique_device;
extern const device_t mystique_220_device;
extern const device_t millennium_ii_device;
#ifdef USE_G100
extern const device_t productiva_g100_device;
#endif /* USE_G100 */

/* JEGA */
extern const device_t if386jega_device;

/* Oak OTI-0x7 */
extern const device_t oti037c_device;
extern const device_t oti037_pbl300sx_device;
extern const device_t oti067_device;
extern const device_t oti067_acer386_device;
extern const device_t oti067_ama932j_device;
extern const device_t oti077_acer100t_device;
extern const device_t oti077_pcs44c_device;
extern const device_t oti077_device;

/* Paradise/WD (S)VGA */
extern const device_t paradise_pvga1a_ncr3302_device;
extern const device_t paradise_pvga1a_pc2086_device;
extern const device_t paradise_pvga1a_pc3086_device;
extern const device_t paradise_pvga1a_device;
extern const device_t paradise_wd90c11_megapc_device;
extern const device_t paradise_wd90c11_device;
extern const device_t paradise_wd90c30_device;

/* Quadram Quadcolor I / I + II */
extern const device_t quadcolor_device;

/* Realtek (S)VGA */
extern const device_t realtek_rtg3105_device;
extern const device_t realtek_rtg3106_device;

/* S3 9XX/8XX/Vision/Trio */
extern const device_t s3_orchid_86c911_isa_device;
extern const device_t s3_diamond_stealth_vram_isa_device;
extern const device_t s3_ami_86c924_isa_device;
extern const device_t s3_elsa_winner1000_86c928_vlb_device;
extern const device_t s3_elsa_winner2000_86c928_isa_device;
extern const device_t s3_metheus_86c928_isa_device;
extern const device_t s3_metheus_86c928_vlb_device;
extern const device_t s3_elsa_winner1000_86c928_pci_device;
extern const device_t s3_spea_mercury_lite_86c928_pci_device;
extern const device_t s3_spea_mirage_86c801_isa_device;
extern const device_t s3_winner1000_805_isa_device;
extern const device_t s3_86c805_onboard_vlb_device;
extern const device_t s3_spea_mirage_86c805_vlb_device;
extern const device_t s3_mirocrystal_8s_805_vlb_device;
extern const device_t s3_mirocrystal_10sd_805_vlb_device;
extern const device_t s3_phoenix_86c801_isa_device;
extern const device_t s3_phoenix_86c805_vlb_device;
extern const device_t s3_bahamas64_vlb_device;
extern const device_t s3_bahamas64_pci_device;
extern const device_t s3_9fx_vlb_device;
extern const device_t s3_9fx_pci_device;
extern const device_t s3_phoenix_trio32_onboard_vlb_device;
extern const device_t s3_phoenix_trio32_vlb_device;
extern const device_t s3_phoenix_trio32_onboard_pci_device;
extern const device_t s3_phoenix_trio32_pci_device;
extern const device_t s3_diamond_stealth_se_vlb_device;
extern const device_t s3_diamond_stealth_se_pci_device;
extern const device_t s3_spea_mirage_p64_vlb_device;
extern const device_t s3_phoenix_trio64_vlb_device;
extern const device_t s3_phoenix_trio64_onboard_pci_device;
extern const device_t s3_phoenix_trio64_pci_device;
extern const device_t s3_stb_powergraph_64_video_vlb_device;
extern const device_t s3_phoenix_trio64vplus_pci_device;
extern const device_t s3_phoenix_trio64vplus_onboard_pci_device;
extern const device_t s3_cardex_trio64vplus_pci_device;
extern const device_t s3_mirocrystal_20sv_964_vlb_device;
extern const device_t s3_mirocrystal_20sv_964_pci_device;
extern const device_t s3_mirocrystal_20sd_864_vlb_device;
extern const device_t s3_phoenix_vision864_pci_device;
extern const device_t s3_phoenix_vision864_vlb_device;
extern const device_t s3_9fx_531_pci_device;
extern const device_t s3_phoenix_vision868_pci_device;
extern const device_t s3_diamond_stealth64_pci_device;
extern const device_t s3_diamond_stealth64_vlb_device;
extern const device_t s3_diamond_stealth64_964_pci_device;
extern const device_t s3_diamond_stealth64_964_vlb_device;
extern const device_t s3_diamond_stealth64_968_pci_device;
extern const device_t s3_diamond_stealth64_968_vlb_device;
extern const device_t s3_mirovideo_40sv_ergo_968_pci_device;
extern const device_t s3_9fx_771_pci_device;
extern const device_t s3_phoenix_vision968_pci_device;
extern const device_t s3_spea_mercury_p64v_pci_device;
extern const device_t s3_elsa_winner2000_pro_x_964_pci_device;
extern const device_t s3_elsa_winner2000_pro_x_pci_device;
extern const device_t s3_trio64v2_dx_pci_device;
extern const device_t s3_trio64v2_dx_onboard_pci_device;

/* S3 ViRGE */
extern const device_t s3_virge_325_pci_device;
extern const device_t s3_virge_325_onboard_pci_device;
extern const device_t s3_diamond_stealth_2000_pci_device;
extern const device_t s3_mirocrystal_3d_pci_device;
extern const device_t s3_diamond_stealth_3000_pci_device;
extern const device_t s3_stb_velocity_3d_pci_device;
extern const device_t s3_virge_375_pci_device;
extern const device_t s3_virge_375_onboard_pci_device;
extern const device_t s3_diamond_stealth_2000pro_pci_device;
extern const device_t s3_virge_385_pci_device;
extern const device_t s3_virge_357_pci_device;
extern const device_t s3_virge_357_agp_device;
extern const device_t s3_diamond_stealth_4000_agp_device;
extern const device_t s3_trio3d2x_pci_device;
extern const device_t s3_trio3d2x_agp_device;

/* Sigma Color 400 */
extern const device_t sigma_device;

/* Trident TGUI 94x0 */
extern const device_t tgui9400cxi_device;
extern const device_t tgui9440_vlb_device;
extern const device_t tgui9440_pci_device;
extern const device_t tgui9440_onboard_pci_device;
extern const device_t tgui9660_pci_device;
extern const device_t tgui9660_onboard_pci_device;
extern const device_t tgui9680_pci_device;

/* IBM PS/1 (S)VGA */
extern const device_t ibm_ps1_2121_device;

/* Trident TVGA 8900 */
extern const device_t tvga8900b_device;
extern const device_t tvga8900d_device;
extern const device_t tvga8900dr_device;
extern const device_t tvga9000b_device;
extern const device_t nec_sv9000_device;

/* IBM VGA */
extern const device_t vga_device;
extern const device_t ps1vga_device;
extern const device_t ps1vga_mca_device;

/* 3DFX Voodoo Graphics */
extern const device_t voodoo_device;
extern const device_t voodoo_banshee_device;
extern const device_t creative_voodoo_banshee_device;
extern const device_t quantum3d_raven_device;
extern const device_t voodoo_3_1000_device;
extern const device_t voodoo_3_1000_agp_device;
extern const device_t voodoo_3_2000_device;
extern const device_t voodoo_3_2000_agp_device;
extern const device_t voodoo_3_2000_agp_onboard_8m_device;
extern const device_t voodoo_3_3000_device;
extern const device_t voodoo_3_3000_agp_device;
extern const device_t voodoo_3_3500_agp_ntsc_device;
extern const device_t voodoo_3_3500_agp_pal_device;
extern const device_t compaq_voodoo_3_3500_agp_device;
extern const device_t voodoo_3_3500_se_agp_device;
extern const device_t voodoo_3_3500_si_agp_device;
extern const device_t velocity_100_agp_device;
extern const device_t velocity_200_agp_device;

/* Wyse 700 */
extern const device_t wy700_device;

/* Yamaha V6355 */
extern const device_t v6355d_device;

/* Tandy */
extern const device_t tandy_1000_video_device;
extern const device_t tandy_1000hx_video_device;
extern const device_t tandy_1000sl_video_device;

#endif

#endif /*EMU_VIDEO_H*/
