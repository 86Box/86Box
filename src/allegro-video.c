/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "allegro-main.h"
#include "ibm.h"
#include "video.h"

#include "allegro-video.h"

static PALETTE cgapal=
{
        {0,0,0},{0,42,0},{42,0,0},{42,21,0},
        {0,0,0},{0,42,42},{42,0,42},{42,42,42},
        {0,0,0},{21,63,21},{63,21,21},{63,63,21},
        {0,0,0},{21,63,63},{63,21,63},{63,63,63},

        {0,0,0},{0,0,42},{0,42,0},{0,42,42},
        {42,0,0},{42,0,42},{42,21,00},{42,42,42},
        {21,21,21},{21,21,63},{21,63,21},{21,63,63},
        {63,21,21},{63,21,63},{63,63,21},{63,63,63},

        {0,0,0},{0,21,0},{0,0,42},{0,42,42},
        {42,0,21},{21,10,21},{42,0,42},{42,0,63},
        {21,21,21},{21,63,21},{42,21,42},{21,63,63},
        {63,0,0},{42,42,0},{63,21,42},{41,41,41},
        
        {0,0,0},{0,42,42},{42,0,0},{42,42,42},
        {0,0,0},{0,42,42},{42,0,0},{42,42,42},
        {0,0,0},{0,63,63},{63,0,0},{63,63,63},
        {0,0,0},{0,63,63},{63,0,0},{63,63,63},
};

static uint32_t pal_lookup[256];

static void allegro_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h);
static void allegro_blit_memtoscreen_8(int x, int y, int w, int h);
static BITMAP *buffer32_vscale;
void allegro_video_init()
{
	int c;

        set_color_depth(32);
        set_gfx_mode(GFX_AUTODETECT_WINDOWED, 640, 480, 0, 0);
        video_blit_memtoscreen = allegro_blit_memtoscreen;
        video_blit_memtoscreen_8 = allegro_blit_memtoscreen_8;

        for (c = 0; c < 256; c++)
        	pal_lookup[c] = makecol(cgapal[c].r << 2, cgapal[c].g << 2, cgapal[c].b << 2);

	buffer32_vscale = create_bitmap(2048, 2048);
}

void allegro_video_close()
{
	destroy_bitmap(buffer32_vscale);
}

void allegro_video_update_size(int x, int y)
{
        if (set_gfx_mode(GFX_AUTODETECT_WINDOWED, x, y, 0, 0))
                fatal("Failed to set gfx mode %i,%i : %s\n", x, y, allegro_error);
}

static void allegro_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h)
{
	if (h < winsizey)
	{
		int yy;

		for (yy = y+y1; yy < y+y2; yy++)
		{
			if (yy >= 0)
			{
				memcpy(&((uint32_t *)buffer32_vscale->line[yy*2])[x], &((uint32_t *)buffer32->line[yy])[x], w*4);
				memcpy(&((uint32_t *)buffer32_vscale->line[(yy*2)+1])[x], &((uint32_t *)buffer32->line[yy])[x], w*4);
			}
		}

	        blit(buffer32_vscale, screen, x, (y+y1)*2, 0, y1, w, (y2-y1)*2);
	}
	else
	        blit(buffer32, screen, x, y+y1, 0, y1, w, y2-y1);
}

static void allegro_blit_memtoscreen_8(int x, int y, int w, int h)
{
	int xx, yy;
	int line_double = (winsizey > h) ? 1 : 0;

	if (y < 0)
	{
		h += y;
		y = 0;
	}

	for (yy = y; yy < y+h; yy++)
	{
		int dy = line_double ? yy*2 : yy;
		if (dy < buffer->h)
		{
			if (line_double)
			{
				for (xx = x; xx < x+w; xx++)
				{
					((uint32_t *)buffer32->line[dy])[xx] =
					((uint32_t *)buffer32->line[dy + 1])[xx] = pal_lookup[buffer->line[yy][xx]];
				}
			}
			else
			{
				for (xx = x; xx < x+w; xx++)
					((uint32_t *)buffer32->line[dy])[xx] = pal_lookup[buffer->line[yy][xx]];
			}
		}
	}

	if (readflash)
	{
		if (line_double)
			rectfill(buffer32, x+SCREEN_W-40, y*2+8, SCREEN_W-8, y*2+14, makecol(255, 255, 255));
		else
			rectfill(buffer32, x+SCREEN_W-40, y+8, SCREEN_W-8, y+14, makecol(255, 255, 255));
		readflash = 0;
	}

	if (line_double)
	        blit(buffer32, screen, x, y*2, 0, 0, w, h*2);
	else
	        blit(buffer32, screen, x, y, 0, 0, w, h);
}
