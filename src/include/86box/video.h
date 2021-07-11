/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the video controller module.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#ifndef EMU_VIDEO_H
# define EMU_VIDEO_H


#define makecol(r, g, b)    ((b) | ((g) << 8) | ((r) << 16))
#define makecol32(r, g, b)  ((b) | ((g) << 8) | ((r) << 16))


enum {
    VID_NONE = 0,
    VID_INTERNAL
};

enum {
    FULLSCR_SCALE_FULL = 0,
    FULLSCR_SCALE_43,
    FULLSCR_SCALE_KEEPRATIO,
    FULLSCR_SCALE_INT
};


#ifdef __cplusplus
extern "C" {
#endif


enum {
    VIDEO_ISA = 0,
    VIDEO_MCA,
    VIDEO_BUS,
    VIDEO_PCI,
    VIDEO_AGP
};

#define VIDEO_FLAG_TYPE_CGA     0
#define VIDEO_FLAG_TYPE_MDA     1
#define VIDEO_FLAG_TYPE_SPECIAL 2
#define VIDEO_FLAG_TYPE_NONE	3
#define VIDEO_FLAG_TYPE_MASK    3

typedef struct {
    int		type;
    int		write_b, write_w, write_l;
    int		read_b, read_w, read_l;
} video_timings_t;

typedef struct {
    int		w, h;
    uint32_t	*dat;
    uint32_t	*line[2112];
} bitmap_t;

typedef struct {
    uint8_t	r, g, b;
} rgb_t;

typedef struct {
    uint8_t	chr[32];
} dbcs_font_t;

typedef rgb_t PALETTE[256];


extern int	changeframecount;

extern volatile int screenshots;
extern bitmap_t	*buffer32, *render_buffer;
extern PALETTE	cgapal,
		cgapal_mono[6];
extern uint32_t	pal_lookup[256];
extern int	video_fullscreen,
		video_fullscreen_scale,
		video_fullscreen_first;
extern int	fullchange;
extern uint8_t	fontdat[2048][8];
extern uint8_t	fontdatm[2048][16];
extern uint8_t	fontdatw[512][32];
extern uint8_t	fontdat8x12[256][16];
extern uint8_t	fontdat12x18[256][36];
extern dbcs_font_t	*fontdatksc5601;
extern dbcs_font_t	*fontdatksc5601_user;
extern uint32_t	*video_6to8,
		*video_8togs,
		*video_8to32,
		*video_15to32,
		*video_16to32;
extern int	xsize,ysize;
extern int	enable_overscan;
extern int	overscan_x,
		overscan_y;
extern int	force_43;
extern int	video_timing_read_b,
		video_timing_read_w,
		video_timing_read_l;
extern int	video_timing_write_b,
		video_timing_write_w,
		video_timing_write_l;
extern int	video_res_x,
		video_res_y,
		video_bpp;
extern int	vid_resize;
extern int	cga_palette,
		herc_blend;
extern int	vid_cga_contrast;
extern int	video_grayscale;
extern int	video_graytype;

extern double	cpuclock;
extern int	emu_fps,
		frames;
extern int	readflash;


/* Function handler pointers. */
extern void	(*video_recalctimings)(void);


/* Table functions. */
extern int	video_card_available(int card);
#ifdef EMU_DEVICE_H
extern const device_t	*video_card_getdevice(int card);
#endif
extern int	video_card_has_config(int card);
extern char	*video_get_internal_name(int card);
extern int	video_get_video_from_internal_name(char *s);
extern int 	video_is_mda(void);
extern int 	video_is_cga(void);
extern int 	video_is_ega_vga(void);
extern void	video_inform(int type, const video_timings_t *ptr);
extern int	video_get_type(void);


extern void	video_setblit(void(*blit)(int,int,int,int,int,int));
extern void	video_blend(int x, int y);
extern void	video_blit_memtoscreen_8(int x, int y, int y1, int y2, int w, int h);
extern void	video_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h);
extern void	video_blit_complete(void);
extern void	video_wait_for_blit(void);
extern void	video_wait_for_buffer(void);

extern bitmap_t	*create_bitmap(int w, int h);
extern void	destroy_bitmap(bitmap_t *b);
extern void	cgapal_rebuild(void);
extern void	hline(bitmap_t *b, int x1, int y, int x2, uint32_t col);
extern void	updatewindowsize(int x, int y);

extern void	video_init(void);
extern void	video_close(void);
extern void	video_reset_close(void);
extern void	video_reset(int card);
extern uint8_t	video_force_resize_get(void);
extern void	video_force_resize_set(uint8_t res);
extern void	video_update_timing(void);

extern void	loadfont_ex(char *s, int format, int offset);
extern void	loadfont(char *s, int format);

extern int	get_actual_size_x(void);
extern int	get_actual_size_y(void);

#ifdef ENABLE_VRAM_DUMP
extern void	svga_dump_vram(void);
#endif

extern uint32_t	video_color_transform(uint32_t color);

#ifdef __cplusplus
}
#endif


#ifdef EMU_DEVICE_H
/* ATi Mach64 */
extern const device_t mach64gx_isa_device;
extern const device_t mach64gx_vlb_device;
extern const device_t mach64gx_pci_device;
extern const device_t mach64vt2_device;

/* ATi 18800 */
#if defined(DEV_BRANCH) && defined(USE_VGAWONDER)
extern const device_t ati18800_wonder_device;
#endif
extern const device_t ati18800_vga88_device;
extern const device_t ati18800_device;

/* ATi 28800 */
extern const device_t ati28800_device;
extern const device_t ati28800k_device;
extern const device_t ati28800k_spc4620p_device;
extern const device_t ati28800k_spc6033p_device;
extern const device_t compaq_ati28800_device;
#if defined(DEV_BRANCH) && defined(USE_XL24)
extern const device_t ati28800_wonderxl24_device;
#endif

/* Cirrus Logic CL-GD 54xx */
extern const device_t gd5401_isa_device;
extern const device_t gd5402_isa_device;
extern const device_t gd5402_onboard_device;
extern const device_t gd5420_isa_device;
extern const device_t gd5422_isa_device;
extern const device_t gd5424_vlb_device;
extern const device_t gd5426_vlb_device;
extern const device_t gd5426_onboard_device;
extern const device_t gd5428_isa_device;
extern const device_t gd5428_vlb_device;
extern const device_t gd5428_mca_device;
extern const device_t gd5428_onboard_device;
extern const device_t gd5429_isa_device;
extern const device_t gd5429_vlb_device;
extern const device_t gd5430_vlb_device;
extern const device_t gd5430_pci_device;
extern const device_t gd5434_isa_device;
extern const device_t gd5434_onboard_pci_device;
extern const device_t gd5434_vlb_device;
extern const device_t gd5434_pci_device;
extern const device_t gd5436_pci_device;
extern const device_t gd5440_onboard_pci_device;
extern const device_t gd5440_pci_device;
extern const device_t gd5446_pci_device;
extern const device_t gd5446_stb_pci_device;
extern const device_t gd5480_pci_device;

/* Compaq CGA */
extern const device_t compaq_cga_device;
extern const device_t compaq_cga_2_device;

/* Olivetti OGC */
extern const device_t ogc_device;
extern const device_t ogc_m24_device;

/* NCR NGA */
extern const device_t nga_device;

/* Tseng ET4000AX */
extern const device_t et4000_isa_device;
extern const device_t et4000k_isa_device;
extern const device_t et4000k_tg286_isa_device;
extern const device_t et4000_kasan_isa_device;
extern const device_t et4000_mca_device;

/* Tseng ET4000-W32x */
extern const device_t et4000w32_device;
extern const device_t et4000w32_onboard_device;
extern const device_t et4000w32i_isa_device;
extern const device_t et4000w32i_vlb_device;
extern const device_t et4000w32p_revc_vlb_device;
extern const device_t et4000w32p_revc_pci_device;
extern const device_t et4000w32p_vlb_device;
extern const device_t et4000w32p_pci_device;
extern const device_t et4000w32p_noncardex_vlb_device;
extern const device_t et4000w32p_noncardex_pci_device;
extern const device_t et4000w32p_cardex_vlb_device;
extern const device_t et4000w32p_cardex_pci_device;

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

#if defined(DEV_BRANCH) && defined(USE_MGA)
/* Matrox Mystique */
extern const device_t mystique_device;
extern const device_t mystique_220_device;
#endif

/* Oak OTI-0x7 */
extern const device_t oti037c_device;
extern const device_t oti067_device;
extern const device_t oti067_acer386_device;
extern const device_t oti067_ama932j_device;
extern const device_t oti067_m300_device;
extern const device_t oti077_device;

/* Paradise/WD (S)VGA */
extern const device_t paradise_pvga1a_ncr3302_device;
extern const device_t paradise_pvga1a_pc2086_device;
extern const device_t paradise_pvga1a_pc3086_device;
extern const device_t paradise_pvga1a_device;
extern const device_t paradise_wd90c11_megapc_device;
extern const device_t paradise_wd90c11_device;
extern const device_t paradise_wd90c30_device;

/* Realtek (S)VGA */
extern const device_t realtek_rtg3106_device;

/* S3 9XX/8XX/Vision/Trio */
extern const device_t s3_orchid_86c911_isa_device;
extern const device_t s3_diamond_stealth_vram_isa_device;
extern const device_t s3_ami_86c924_isa_device;
extern const device_t s3_metheus_86c928_isa_device;
extern const device_t s3_metheus_86c928_vlb_device;
extern const device_t s3_v7mirage_86c801_isa_device;
extern const device_t s3_phoenix_86c805_vlb_device;
extern const device_t s3_bahamas64_vlb_device;
extern const device_t s3_bahamas64_pci_device;
extern const device_t s3_9fx_vlb_device;
extern const device_t s3_9fx_pci_device;
extern const device_t s3_phoenix_trio32_vlb_device;
extern const device_t s3_phoenix_trio32_pci_device;
extern const device_t s3_diamond_stealth_se_vlb_device;
extern const device_t s3_diamond_stealth_se_pci_device;
extern const device_t s3_phoenix_trio64_vlb_device;
extern const device_t s3_phoenix_trio64_onboard_pci_device;
extern const device_t s3_phoenix_trio64_pci_device;
extern const device_t s3_phoenix_trio64vplus_vlb_device;
extern const device_t s3_phoenix_trio64vplus_pci_device;
extern const device_t s3_phoenix_trio64vplus_onboard_pci_device;
extern const device_t s3_phoenix_vision864_pci_device;
extern const device_t s3_phoenix_vision864_vlb_device;
extern const device_t s3_phoenix_vision868_pci_device;
extern const device_t s3_phoenix_vision868_vlb_device;
extern const device_t s3_diamond_stealth64_pci_device;
extern const device_t s3_diamond_stealth64_vlb_device;
extern const device_t s3_diamond_stealth64_964_pci_device;
extern const device_t s3_diamond_stealth64_964_vlb_device;
extern const device_t s3_elsa_winner2000_pro_x_964_pci_device;
extern const device_t s3_elsa_winner2000_pro_x_964_vlb_device;
extern const device_t s3_elsa_winner2000_pro_x_pci_device;
extern const device_t s3_elsa_winner2000_pro_x_vlb_device;
extern const device_t s3_trio64v2_dx_pci_device;

/* S3 ViRGE */
extern const device_t s3_virge_325_vlb_device;
extern const device_t s3_virge_325_pci_device;
extern const device_t s3_virge_vlb_device;
extern const device_t s3_virge_pci_device;
extern const device_t s3_virge_988_vlb_device;
extern const device_t s3_virge_988_pci_device;
extern const device_t s3_virge_375_vlb_device;
extern const device_t s3_virge_375_pci_device;
extern const device_t s3_virge_375_4_vlb_device;
extern const device_t s3_virge_375_4_pci_device;
#if defined(DEV_BRANCH) && defined(USE_S3TRIO3D2X)
extern const device_t s3_trio3d_2x_pci_device;
#endif

/* Sigma Color 400 */
extern const device_t sigma_device;

/* Trident TGUI 94x0 */
extern const device_t tgui9400cxi_device;
extern const device_t tgui9440_vlb_device;
extern const device_t tgui9440_pci_device;
extern const device_t tgui9660_pci_device;
extern const device_t tgui9680_pci_device;

/* IBM PS/1 (S)VGA */
extern const device_t ibm_ps1_2121_device;

/* Trident TVGA 8900 */
extern const device_t tvga8900b_device;
extern const device_t tvga8900d_device;
extern const device_t tvga9000b_device;

/* IBM VGA */
extern const device_t vga_device;
extern const device_t ps1vga_device;
extern const device_t ps1vga_mca_device;

/* 3DFX Voodoo Graphics */
extern const device_t voodoo_device;
extern const device_t voodoo_banshee_device;
extern const device_t creative_voodoo_banshee_device;
extern const device_t voodoo_3_2000_device;
extern const device_t voodoo_3_2000_agp_device;
extern const device_t voodoo_3_2000_agp_onboard_8m_device;
extern const device_t voodoo_3_3000_device;
extern const device_t voodoo_3_3000_agp_device;

/* Wyse 700 */
extern const device_t wy700_device;
#endif


#endif	/*EMU_VIDEO_H*/
