/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#ifdef __unix

#include "allegro-main.h"

#else

typedef struct
{
        int w, h;
        uint8_t *dat;
        uint8_t *line[];
} BITMAP;

extern BITMAP *screen;

BITMAP *create_bitmap(int w, int h);

typedef struct
{
        uint8_t r, g, b;
} RGB;
        
typedef RGB PALETTE[256];

#define makecol(r, g, b)    ((b) | ((g) << 8) | ((r) << 16))
#define makecol32(r, g, b)  ((b) | ((g) << 8) | ((r) << 16))

#endif

extern BITMAP *buffer, *buffer32;

int video_card_available(int card);
char *video_card_getname(int card);
struct device_t *video_card_getdevice(int card);
int video_card_has_config(int card);
int video_card_getid(char *s);
int video_old_to_new(int card);
int video_new_to_old(int card);
char *video_get_internal_name(int card);
int video_get_video_from_internal_name(char *s);

extern int video_fullscreen, video_fullscreen_scale, video_fullscreen_first;

enum
{
        FULLSCR_SCALE_FULL = 0,
        FULLSCR_SCALE_43,
        FULLSCR_SCALE_SQ,
        FULLSCR_SCALE_INT
};

extern int egareads,egawrites;

extern int fullchange;
extern int changeframecount;

extern uint8_t fontdat[256][8];
extern uint8_t fontdatm[256][16];

extern uint32_t *video_6to8, *video_15to32, *video_16to32;

extern int xsize,ysize;

extern float cpuclock;

extern int emu_fps, frames;

extern int readflash;

extern void (*video_recalctimings)();

void video_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h);
void video_blit_memtoscreen_8(int x, int y, int w, int h);

extern void (*video_blit_memtoscreen_func)(int x, int y, int y1, int y2, int w, int h);
extern void (*video_blit_memtoscreen_8_func)(int x, int y, int w, int h);

/* Enable EGA/(S)VGA overscan border. */
extern int enable_overscan;
extern int overscan_x, overscan_y;
/* Forcibly stretch emulated video output to 4:3 or not. */
extern int force_43;
/* Enable disk activity flash. */
extern int enable_flash;

extern int video_timing_b, video_timing_w, video_timing_l;
extern int video_speed;

extern int video_res_x, video_res_y, video_bpp;

extern int vid_resize;

void video_wait_for_blit();
void video_wait_for_buffer();

extern int winsizex,winsizey;

#ifdef __cplusplus
extern "C" {
#endif
void take_screenshot();

void d3d_take_screenshot(char *fn);
void d3d_fs_take_screenshot(char *fn);
void ddraw_take_screenshot(char *fn);
void ddraw_fs_take_screenshot(char *fn);
#ifdef __cplusplus
}
#endif

extern int cga_palette;
