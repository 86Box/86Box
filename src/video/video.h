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
 * Version:	@(#)video.h	1.0.36	2018/10/28
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
    VID_INTERNAL
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
extern int	cga_palette,
		herc_blend;
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
