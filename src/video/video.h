#ifndef EMU_VIDEO_H
# define EMU_VIDEO_H


#define makecol(r, g, b)    ((b) | ((g) << 8) | ((r) << 16))
#define makecol32(r, g, b)  ((b) | ((g) << 8) | ((r) << 16))


enum {
    FULLSCR_SCALE_FULL = 0,
    FULLSCR_SCALE_43,
    FULLSCR_SCALE_SQ,
    FULLSCR_SCALE_INT
};


typedef struct {
    int		w, h;
    uint8_t	*dat;
    uint8_t	*line[];
} BITMAP;

typedef struct {
    uint8_t	r, g, b;
} RGB;
        
typedef RGB PALETTE[256];


extern BITMAP	*screen,
		*buffer,
		*buffer32;
extern PALETTE	cgapal,
		cgapal_mono[6];
extern uint32_t	pal_lookup[256];
extern int	video_fullscreen,
		video_fullscreen_scale,
		video_fullscreen_first;
extern int	egareads,egawrites;
extern int	fullchange;
extern int	changeframecount;
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


#ifdef __cplusplus
extern "C" {
#endif

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


extern void	video_setblit(void(*blit8)(int,int,int,int),
			      void(*blit)(int,int,int,int,int,int));
extern void	video_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h);
extern void	video_blit_memtoscreen_8(int x, int y, int w, int h);
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
extern uint8_t	video_force_resize_get(void);
extern void	video_force_resize_set(uint8_t res);
extern void	video_reset_device(int, int);
extern void	video_update_timing(void);

extern void	loadfont(wchar_t *s, int format);

#ifdef ENABLE_VRAM_DUMP
extern void	svga_dump_vram(void);
#endif

#ifdef __cplusplus
}
#endif


#endif	/*EMU_VIDEO_H*/

