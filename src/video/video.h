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
 * Version:	@(#)video.h	1.0.35	2018/09/19
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#ifndef EMU_VIDEO_H
# define EMU_VIDEO_H


#define makecol(r, g, b)    ((b) | ((g) << 8) | ((r) << 16))
#define makecol32(r, g, b)  ((b) | ((g) << 8) | ((r) << 16))


enum {
    VID_NONE = 0,
    VID_INTERNAL,
    VID_CGA,
    VID_COMPAQ_CGA,		/* Compaq CGA */
    VID_COMPAQ_CGA_2,		/* Compaq CGA 2 */
    VID_COLORPLUS,		/* Plantronics ColorPlus */
    VID_WY700,			/* Wyse 700 */
    VID_MDA,
    VID_GENIUS,			/* MDSI Genius */
    VID_HERCULES,
    VID_HERCULESPLUS,
    VID_INCOLOR,		/* Hercules InColor */
    VID_EGA,			/* Using IBM EGA BIOS */
    VID_COMPAQ_EGA,		/* Compaq EGA */
    VID_SUPER_EGA,		/* Using Chips & Technologies SuperEGA BIOS */
    VID_VGA,        		/* IBM VGA */
    VID_TVGA,			/* Using Trident TVGA8900D BIOS */
    VID_ET4000_ISA,		/* Tseng ET4000 */
    VID_ET4000_MCA,		/* Tseng ET4000 */
    VID_TGKOREANVGA,		/*Trigem Korean VGA(Tseng ET4000AX)*/	
    VID_ET4000W32_CARDEX_VLB,	/* Tseng ET4000/W32p (Cardex) VLB */
    VID_ET4000W32_CARDEX_PCI,	/* Tseng ET4000/W32p (Cardex) PCI */
#if defined(DEV_BRANCH) && defined(USE_STEALTH32)
    VID_ET4000W32_VLB,		/* Tseng ET4000/W32p (Diamond Stealth 32) VLB */
    VID_ET4000W32_PCI,		/* Tseng ET4000/W32p (Diamond Stealth 32) PCI */
#endif
    VID_BAHAMAS64_VLB,		/* S3 Vision864 (Paradise Bahamas 64) VLB */
    VID_BAHAMAS64_PCI,		/* S3 Vision864 (Paradise Bahamas 64) PCI */
    VID_N9_9FX_VLB,		/* S3 764/Trio64 (Number Nine 9FX) VLB */
    VID_N9_9FX_PCI,		/* S3 764/Trio64 (Number Nine 9FX) PCI */
    VID_TGUI9400CXI,   		/* Trident TGUI9400CXi VLB */
    VID_TGUI9440_VLB,   	/* Trident TGUI9440AGi VLB */
    VID_TGUI9440_PCI,   	/* Trident TGUI9440AGi PCI */
    VID_ATIKOREANVGA,		/*ATI Korean VGA (28800-5)*/
    VID_VGA88,  		/* ATI VGA-88 (18800-1) */
    VID_VGAEDGE16,  		/* ATI VGA Edge-16 (18800-1) */
    VID_VGACHARGER, 		/* ATI VGA Charger (28800-5) */
#if defined(DEV_BRANCH) && defined(USE_VGAWONDER)
    VID_VGAWONDER,		/* Compaq ATI VGA Wonder (18800) */
#endif
    VID_VGAWONDERXL,		/* Compaq ATI VGA Wonder XL (28800-5) */
#if defined(DEV_BRANCH) && defined(USE_XL24)
    VID_VGAWONDERXL24,		/* Compaq ATI VGA Wonder XL24 (28800-6) */
#endif
    VID_MACH64GX_ISA,		/* ATI Graphics Pro Turbo (Mach64) ISA */
    VID_MACH64GX_VLB,		/* ATI Graphics Pro Turbo (Mach64) VLB */
    VID_MACH64GX_PCI,		/* ATI Graphics Pro Turbo (Mach64) PCI */
    VID_MACH64VT2,  		/* ATI Mach64 VT2 */
    VID_CL_GD5424_ISA, 		/* Cirrus Logic CL-GD 5424 ISA */
    VID_CL_GD5424_VLB, 		/* Cirrus Logic CL-GD 5424 VLB */
    VID_CL_GD5426_VLB, 		/* Diamond SpeedStar PRO (Cirrus Logic CL-GD 5426) VLB */
    VID_CL_GD5428_ISA, 		/* Cirrus Logic CL-GD 5428 ISA */
    VID_CL_GD5428_VLB,		/* Cirrus Logic CL-GD 5428 VLB */
    VID_CL_GD5429_ISA, 		/* Cirrus Logic CL-GD 5429 ISA */
    VID_CL_GD5429_VLB,		/* Cirrus Logic CL-GD 5429 VLB */
    VID_CL_GD5430_VLB,		/* Diamond SpeedStar PRO SE (Cirrus Logic CL-GD 5430) VLB */
    VID_CL_GD5430_PCI,		/* Cirrus Logic CL-GD 5430 PCI */
    VID_CL_GD5434_ISA, 		/* Cirrus Logic CL-GD 5434 ISA */
    VID_CL_GD5434_VLB,		/* Cirrus Logic CL-GD 5434 VLB */
    VID_CL_GD5434_PCI,		/* Cirrus Logic CL-GD 5434 PCI */
    VID_CL_GD5436_PCI,		/* Cirrus Logic CL-GD 5436 PCI */
    VID_CL_GD5440_PCI,		/* Cirrus Logic CL-GD 5440 PCI */
    VID_CL_GD5446_PCI,		/* Cirrus Logic CL-GD 5446 PCI */
    VID_CL_GD5446_STB_PCI,	/* STB Nitro 64V (Cirrus Logic CL-GD 5446) PCI */
    VID_CL_GD5480_PCI,		/* Cirrus Logic CL-GD 5480 PCI */
    VID_EXPERTCOLOR_VLB,	/* S3 Vision868 (ExpertColor DSV3868P CF55) VLB */
    VID_EXPERTCOLOR_PCI,	/* S3 Vision868 (ExpertColor DSV3868P CF55) PCI */
    VID_OTI037C,     		/* Oak OTI-037C */
    VID_OTI067,     		/* Oak OTI-067 */
    VID_OTI077,     		/* Oak OTI-077 */
    VID_PVGA1A,			/* Paradise PVGA1A Standalone */
    VID_WD90C11,		/* Paradise WD90C11-LR Standalone */
    VID_WD90C30,		/* Paradise WD90C30-LR Standalone */
    VID_PHOENIX_VISION864_VLB,	/* S3 Vision864 (Phoenix) VLB */
    VID_PHOENIX_VISION864_PCI,	/* S3 Vision864 (Phoenix) PCI */
    VID_PHOENIX_TRIO32_VLB, 	/* S3 732/Trio32 (Phoenix) VLB */
    VID_PHOENIX_TRIO32_PCI, 	/* S3 732/Trio32 (Phoenix) PCI */
    VID_PHOENIX_TRIO64_VLB, 	/* S3 764/Trio64 (Phoenix) VLB */
    VID_PHOENIX_TRIO64_PCI, 	/* S3 764/Trio64 (Phoenix) PCI */
    VID_STEALTH64_VLB,		/* S3 Trio64 (Diamond Stealth 64) VLB */
    VID_STEALTH64_PCI,		/* S3 Trio64 (Diamond Stealth 64) PCI */
#if defined(DEV_BRANCH) && defined(USE_TI)
    VID_TICF62011,  		/* TI CF62011 */
#endif
    VID_VIRGE_VLB,      	/* S3 Virge VLB */
    VID_VIRGE_PCI,      	/* S3 Virge PCI */
    VID_VIRGEDX_VLB,    	/* S3 Virge/DX VLB */
    VID_VIRGEDX_PCI,    	/* S3 Virge/DX PCI */
    VID_VIRGEDX4_VLB,		/* S3 Virge/DX (VBE 2.0) VLB */
    VID_VIRGEDX4_PCI,		/* S3 Virge/DX (VBE 2.0) PCI */
    VID_VIRGEVX_VLB,		/* S3 Virge/VX VLB */
    VID_VIRGEVX_PCI,		/* S3 Virge/VX PCI */

    VID_MAX
};

enum {
    FULLSCR_SCALE_FULL = 0,
    FULLSCR_SCALE_43,
    FULLSCR_SCALE_SQ,
    FULLSCR_SCALE_INT,
    FULLSCR_SCALE_KEEPRATIO
};


#ifdef __cplusplus
extern "C" {
#endif


enum {
    VIDEO_ISA = 0,
    VIDEO_MCA,
    VIDEO_BUS
};

#define VIDEO_FLAG_TYPE_CGA     0
#define VIDEO_FLAG_TYPE_MDA     1
#define VIDEO_FLAG_TYPE_SPECIAL 2
#define VIDEO_FLAG_TYPE_MASK    3

typedef struct {
    int		type;
    int		write_b, write_w, write_l;
    int		read_b, read_w, read_l;
} video_timings_t;

typedef struct {
    int		w, h;
    uint8_t	*dat;
    uint8_t	*line[2048];
} bitmap_t;

typedef struct {
    uint8_t	r, g, b;
} rgb_t;

typedef struct {
    uint8_t	chr[32];
} dbcs_font_t;

typedef rgb_t PALETTE[256];


extern int	gfx_present[VID_MAX];
extern int	egareads,
		egawrites;
extern int	changeframecount;

extern bitmap_t	*screen,
		*buffer,
		*buffer32;
extern PALETTE	cgapal,
		cgapal_mono[6];
extern uint32_t	pal_lookup[256];
extern int	video_fullscreen,
		video_fullscreen_scale,
		video_fullscreen_first;
extern int	fullchange;
extern uint8_t	fontdat[2048][8];
extern uint8_t	fontdatm[2048][16];
extern dbcs_font_t	*fontdatksc5601;
extern dbcs_font_t	*fontdatksc5601_user;
extern uint32_t	*video_6to8,
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
extern int	cga_palette;
extern int	vid_cga_contrast;
extern int	video_grayscale;
extern int	video_graytype;

extern float	cpuclock;
extern int	emu_fps,
		frames;
extern int	readflash;


/* Function handler pointers. */
extern void	(*video_recalctimings)(void);


/* Table functions. */
extern int	video_card_available(int card);
extern char	*video_card_getname(int card);
#ifdef EMU_DEVICE_H
extern const device_t	*video_card_getdevice(int card);
#endif
extern int	video_card_has_config(int card);
extern int	video_card_getid(char *s);
extern int	video_old_to_new(int card);
extern int	video_new_to_old(int card);
extern char	*video_get_internal_name(int card);
extern int	video_get_video_from_internal_name(char *s);
extern int 	video_is_mda(void);
extern int 	video_is_cga(void);
extern int 	video_is_ega_vga(void);
extern void	video_inform(int type, const video_timings_t *ptr);
extern int	video_get_type(void);


extern void	video_setblit(void(*blit)(int,int,int,int,int,int));
extern void	video_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h);
extern void	video_blit_memtoscreen_8(int x, int y, int y1, int y2, int w, int h);
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
extern void	video_reset(int card);
extern uint8_t	video_force_resize_get(void);
extern void	video_force_resize_set(uint8_t res);
extern void	video_update_timing(void);

extern void	loadfont(wchar_t *s, int format);

extern int	get_actual_size_x(void);
extern int	get_actual_size_y(void);

#ifdef ENABLE_VRAM_DUMP
extern void	svga_dump_vram(void);
#endif

extern uint32_t	video_color_transform(uint32_t color);
extern void	video_transform_copy(uint32_t *dst, uint32_t *src, int len);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_VIDEO_H*/
