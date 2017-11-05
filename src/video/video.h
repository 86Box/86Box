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
 * Version:	@(#)video.h	1.0.2	2017/11/04
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_VIDEO_H
# define EMU_VIDEO_H


#define makecol(r, g, b)    ((b) | ((g) << 8) | ((r) << 16))
#define makecol32(r, g, b)  ((b) | ((g) << 8) | ((r) << 16))


enum {
    GFX_NONE = 0,
    GFX_INTERNAL,
    GFX_CGA,
    GFX_MDA,
    GFX_HERCULES,
    GFX_EGA,				/* Using IBM EGA BIOS */
    GFX_TVGA,				/* Using Trident TVGA8900D BIOS */
    GFX_ET4000,				/* Tseng ET4000 */
    GFX_ET4000W32_VLB,			/* Tseng ET4000/W32p (Diamond Stealth 32) VLB */
    GFX_ET4000W32_PCI,			/* Tseng ET4000/W32p (Diamond Stealth 32) PCI */
    GFX_BAHAMAS64_VLB,			/* S3 Vision864 (Paradise Bahamas 64) VLB */
    GFX_BAHAMAS64_PCI,			/* S3 Vision864 (Paradise Bahamas 64) PCI */
    GFX_N9_9FX_VLB,			/* S3 764/Trio64 (Number Nine 9FX) VLB */
    GFX_N9_9FX_PCI,			/* S3 764/Trio64 (Number Nine 9FX) PCI */
    GFX_VIRGE_VLB,      		/* S3 Virge VLB */
    GFX_VIRGE_PCI,      		/* S3 Virge PCI */
    GFX_TGUI9440_VLB,   		/* Trident TGUI9440 VLB */
    GFX_TGUI9440_PCI,   		/* Trident TGUI9440 PCI */
    GFX_VGA,        			/* IBM VGA */
    GFX_VGAEDGE16,  			/* ATI VGA Edge-16 (18800-1) */
    GFX_VGACHARGER, 			/* ATI VGA Charger (28800-5) */
    GFX_OTI067,     			/* Oak OTI-067 */
    GFX_MACH64GX_VLB,			/* ATI Graphics Pro Turbo (Mach64) VLB */
    GFX_MACH64GX_PCI,			/* ATI Graphics Pro Turbo (Mach64) PCI */
    GFX_CL_GD5429,  			/* Cirrus Logic CL-GD5429 */
    GFX_VIRGEDX_VLB,    		/* S3 Virge/DX VLB */
    GFX_VIRGEDX_PCI,    		/* S3 Virge/DX PCI */
    GFX_PHOENIX_TRIO32_VLB, 		/* S3 732/Trio32 (Phoenix) VLB */
    GFX_PHOENIX_TRIO32_PCI, 		/* S3 732/Trio32 (Phoenix) PCI */
    GFX_PHOENIX_TRIO64_VLB, 		/* S3 764/Trio64 (Phoenix) VLB */
    GFX_PHOENIX_TRIO64_PCI, 		/* S3 764/Trio64 (Phoenix) PCI */
    GFX_INCOLOR,			/* Hercules InColor */
    GFX_COLORPLUS,			/* Plantronics ColorPlus */
    GFX_WY700,				/* Wyse 700 */
    GFX_GENIUS,				/* MDSI Genius */
    GFX_MACH64VT2,  			/* ATI Mach64 VT2 */
    GFX_COMPAQ_EGA,			/* Compaq EGA */
    GFX_SUPER_EGA,			/* Using Chips & Technologies SuperEGA BIOS */
    GFX_COMPAQ_VGA,			/* Compaq/Paradise VGA */
    GFX_CL_GD5446,			/* Cirrus Logic CL-GD5446 */
    GFX_VGAWONDERXL,			/* Compaq ATI VGA Wonder XL (28800-5) */
    GFX_WD90C11,			/* Paradise WD90C11 Standalone */
    GFX_OTI077,     			/* Oak OTI-077 */
    GFX_VGAWONDERXL24,			/* Compaq ATI VGA Wonder XL24 (28800-6) */
    GFX_STEALTH64_VLB,			/* S3 Vision864 (Diamond Stealth 64) VLB */
    GFX_STEALTH64_PCI,			/* S3 Vision864 (Diamond Stealth 64) PCI */
    GFX_PHOENIX_VISION864_VLB,		/* S3 Vision864 (Phoenix) VLB */
    GFX_PHOENIX_VISION864_PCI,		/* S3 Vision864 (Phoenix) PCI */
    GFX_RIVATNT,			/* nVidia Riva TNT */
    GFX_RIVATNT2,			/* nVidia Riva TNT2 */
    GFX_RIVA128,			/* nVidia Riva 128 */
    GFX_HERCULESPLUS,
    GFX_VIRGEVX_VLB,			/* S3 Virge/VX VLB */
    GFX_VIRGEVX_PCI,			/* S3 Virge/VX PCI */
    GFX_VIRGEDX4_VLB,			/* S3 Virge/DX (VBE 2.0) VLB */
    GFX_VIRGEDX4_PCI,			/* S3 Virge/DX (VBE 2.0) PCI */
    GFX_OTI037,				/* Oak OTI-037 */
    GFX_TRIGEM_UNK,			/* Unknown TriGem graphics card w/Hangeul ROM */
    GFX_MIRO_VISION964, 		/* S3 Vision964 (Miro Crystal) */
    GFX_CL_GD5422,  			/* Cirrus Logic CL-GD5422 */
    GFX_CL_GD5430,  			/* Cirrus Logic CL-GD5430 */
    GFX_CL_GD5434,  			/* Cirrus Logic CL-GD5434 */
    GFX_CL_GD5436,  			/* Cirrus Logic CL-GD5436 */
    GFX_CL_GD5440,  			/* Cirrus Logic CL-GD5440 */

    GFX_MAX
};

#define MDA	((gfxcard==GFX_MDA || gfxcard==GFX_HERCULES || \
		  gfxcard==GFX_HERCULESPLUS || gfxcard==GFX_INCOLOR || \
		  gfxcard==GFX_GENIUS) && (romset<ROM_TANDY || romset>=ROM_IBMAT))

#define VGA	((gfxcard>=GFX_TVGA) && gfxcard!=GFX_COLORPLUS && \
		  gfxcard!=GFX_INCOLOR && gfxcard!=GFX_WY700 && gfxcard!=GFX_GENIUS && \
		  gfxcard!=GFX_COMPAQ_EGA && gfxcard!=GFX_SUPER_EGA && \
		  gfxcard!=GFX_HERCULESPLUS && romset!=ROM_PC1640 && \
		  romset!=ROM_PC1512 && romset!=ROM_TANDY && romset!=ROM_PC200)

enum {
    FULLSCR_SCALE_FULL = 0,
    FULLSCR_SCALE_43,
    FULLSCR_SCALE_SQ,
    FULLSCR_SCALE_INT
};


#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
    int		w, h;
    uint8_t	*dat;
    uint8_t	*line[];
} BITMAP;

typedef struct {
    uint8_t	r, g, b;
} RGB;

typedef RGB PALETTE[256];


extern int	gfx_present[GFX_MAX];
extern int	egareads,
		egawrites;
extern int	changeframecount;

extern BITMAP	*screen,
		*buffer,
		*buffer32;
extern PALETTE	cgapal,
		cgapal_mono[6];
extern uint32_t	pal_lookup[256];
extern int	video_fullscreen,
		video_fullscreen_scale,
		video_fullscreen_first;
extern int	fullchange;
extern uint8_t	fontdat[256][8];
extern uint8_t	fontdatm[256][16];
extern uint32_t	*video_6to8,
		*video_15to32,
		*video_16to32;
extern int	xsize,ysize;
extern int	enable_overscan;
extern int	overscan_x,
		overscan_y;
extern int	force_43;
extern int	video_timing_b,
		video_timing_w,
		video_timing_l;
extern int	video_speed;
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
extern device_t	*video_card_getdevice(int card);
#endif
extern int	video_card_has_config(int card);
extern int	video_card_getid(char *s);
extern int	video_old_to_new(int card);
extern int	video_new_to_old(int card);
extern char	*video_get_internal_name(int card);
extern int	video_get_video_from_internal_name(char *s);


extern void	video_setblit(void(*blit)(int,int,int,int,int,int));
extern void	video_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h);
extern void	video_blit_memtoscreen_8(int x, int y, int y1, int y2, int w, int h);
extern void	video_blit_complete(void);
extern void	video_wait_for_blit(void);
extern void	video_wait_for_buffer(void);

extern BITMAP	*create_bitmap(int w, int h);
extern void	destroy_bitmap(BITMAP *b);
extern void	cgapal_rebuild(void);
extern void	hline(BITMAP *b, int x1, int y, int x2, uint32_t col);
extern void	updatewindowsize(int x, int y);

extern void	video_init(void);
extern void	video_close(void);
extern void	video_reset(void);
extern void	video_reset_card(int);
extern uint8_t	video_force_resize_get(void);
extern void	video_force_resize_set(uint8_t res);
extern void	video_update_timing(void);

extern void	loadfont(wchar_t *s, int format);

#ifdef ENABLE_VRAM_DUMP
extern void	svga_dump_vram(void);
#endif

#ifdef __cplusplus
}
#endif


#endif	/*EMU_VIDEO_H*/
