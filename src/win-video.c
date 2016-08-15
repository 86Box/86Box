/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "video.h"

BITMAP *screen;

void hline(BITMAP *b, int x1, int y, int x2, uint32_t col)
{
        if (y < 0 || y >= buffer->h)
           return;
           
        if (b == buffer)
           memset(&b->line[y][x1], col, x2 - x1);
        else
           memset(&((uint32_t *)b->line[y])[x1], col, (x2 - x1) * 4);
}

void blit(BITMAP *src, BITMAP *dst, int x1, int y1, int x2, int y2, int xs, int ys)
{
}

void stretch_blit(BITMAP *src, BITMAP *dst, int x1, int y1, int xs1, int ys1, int x2, int y2, int xs2, int ys2)
{
}

void rectfill(BITMAP *b, int x1, int y1, int x2, int y2, uint32_t col)
{
}

void set_palette(PALETTE p)
{
}

void destroy_bitmap(BITMAP *b)
{
}

BITMAP *create_bitmap(int x, int y)
{
        BITMAP *b = malloc(sizeof(BITMAP) + (y * sizeof(uint8_t *)));
        int c;
        b->dat = malloc(x * y * 4);
        for (c = 0; c < y; c++)
        {
                b->line[c] = b->dat + (c * x * 4);
        }
        b->w = x;
        b->h = y;
        return b;
}
