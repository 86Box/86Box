/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdio.h>
#include "../ibm.h"
#include "../mem.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_svga_render.h"


int invert_display = 0;


uint32_t svga_color_transform(uint32_t color)
{
	uint32_t temp = 0;
	if (invert_display)
	{
		temp |= (0xff - (color & 0xff));
		temp |= (0xff00 - (color & 0xff00));
		temp |= (0xff0000 - (color & 0xff0000));
	}
	else
	{
		temp = color;
	}
	return temp;
}

int svga_display_line(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	unsigned int dl = svga->displine;
	if (svga->crtc[9] & 0x1f)
	{
		dl -= (svga->crtc[8] & 0x1f);
	}
	dl += y_add;
	dl &= 0x7ff;
	return dl;
}

void svga_render_blank(svga_t *svga)
{
        int x, xx;
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);
        
        if (svga->firstline_draw == 2000) 
                svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;
        
        for (x = 0; x < svga->hdisp; x++)
        {
                switch (svga->seqregs[1] & 9)
                {
                        case 0:
                        for (xx = 0; xx < 9; xx++) ((uint32_t *)buffer32->line[dl])[(x * 9) + xx + 32 + x_add] = svga_color_transform(0);
                        break;
                        case 1:
                        for (xx = 0; xx < 8; xx++) ((uint32_t *)buffer32->line[dl])[(x * 8) + xx + 32 + x_add] = svga_color_transform(0);
                        break;
                        case 8:
                        for (xx = 0; xx < 18; xx++) ((uint32_t *)buffer32->line[dl])[(x * 18) + xx + 32 + x_add] = svga_color_transform(0);
                        break;
                        case 9:
                        for (xx = 0; xx < 16; xx++) ((uint32_t *)buffer32->line[dl])[(x * 16) + xx + 32 + x_add] = svga_color_transform(0);
                        break;
                }
        }
}

void svga_render_text_40(svga_t *svga)
{     
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->firstline_draw == 2000) 
                svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;
        
        if (svga->fullchange)
        {
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[32 + x_add];
                int x, xx;
                int drawcursor;
                uint8_t chr, attr, dat;
                uint32_t charaddr;
                int fg, bg;
                int xinc = (svga->seqregs[1] & 1) ? 16 : 18;
                
                for (x = 0; x < svga->hdisp; x += xinc)
                {
                        drawcursor = ((svga->ma == svga->ca) && svga->con && svga->cursoron);
			chr = svga->vram[svga->ma << 1];
			attr = svga->vram[(svga->ma << 1) + 1];
                        if (attr & 8) charaddr = svga->charsetb + (chr * 128);
                        else          charaddr = svga->charseta + (chr * 128);

                        if (drawcursor) 
                        { 
                                bg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask]; 
                                fg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask]; 
                        }
                        else
                        {
                                fg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask];
                                bg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask];
                                if (attr & 0x80 && svga->attrregs[0x10] & 8)
                                {
                                        bg = svga->pallook[svga->egapal[(attr >> 4) & 7] & svga->dac_mask];
                                        if (svga->blink & 16) 
                                                fg = bg;
                                }
                        }

                        dat = svga->vram[charaddr + (svga->sc << 2)];
                        if (svga->seqregs[1] & 1) 
                        { 
                                for (xx = 0; xx < 16; xx += 2) 
                                        p[xx] = p[xx + 1] = (dat & (0x80 >> (xx >> 1))) ? svga_color_transform(fg) : svga_color_transform(bg);
                        }
                        else
                        {
                                for (xx = 0; xx < 16; xx += 2)
                                        p[xx] = p[xx + 1] = (dat & (0x80 >> (xx >> 1))) ? svga_color_transform(fg) : svga_color_transform(bg);
                                if ((chr & ~0x1F) != 0xC0 || !(svga->attrregs[0x10] & 4)) 
                                        p[16] = p[17] = svga_color_transform(bg);
                                else                  
                                        p[16] = p[17] = (dat & 1) ? svga_color_transform(fg) : svga_color_transform(bg);
                        }
                        svga->ma += 4; 
	                svga->ma = svga_mask_addr(svga->ma, svga);
                        p += xinc;
                }
        }
}

#if 0
void svga_render_text_40_12(svga_t *svga)
{     
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = (enable_overscan) ? 12 : 0;
	int dl = svga_display_line(svga);

        if (svga->firstline_draw == 2000) 
                svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;
        
        if (svga->fullchange)
        {
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[32 + x_add];
                int x, xx;
                int drawcursor;
                uint8_t chr, attr;
		uint16_t dat;
                uint32_t charaddr;
                int fg, bg;
                int xinc = 24;
                
                for (x = 0; x < svga->hdisp; x += xinc)
                {
                        drawcursor = ((svga->ma == svga->ca) && svga->con && svga->cursoron);
			chr = svga->vram[svga->ma << 1];
			attr = svga->vram[(svga->ma << 1) + 1];
                        if (attr & 8) charaddr = svga->charsetb + (chr * 128);
                        else          charaddr = svga->charseta + (chr * 128);

                        if (drawcursor) 
                        { 
                                bg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask]; 
                                fg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask]; 
                        }
                        else
                        {
                                fg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask];
                                bg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask];
                                if (attr & 0x80 && svga->attrregs[0x10] & 8)
                                {
                                        bg = svga->pallook[svga->egapal[(attr >> 4) & 7] & svga->dac_mask];
                                        if (svga->blink & 16) 
                                                fg = bg;
                                }
                        }

                        dat = *(uint16_t *) &(svga->vram[charaddr + (svga->sc << 2) - 1]);
			for (xx = 0; xx < 24; xx += 2) 
				p[xx] = p[xx + 1] = (dat & (0x800 >> (xx >> 1))) ? svga_color_transform(fg) : svga_color_transform(bg);
                        svga->ma += 4; 
	                svga->ma = svga_mask_addr(svga->ma, svga);
                        p += xinc;
                }
        }
}
#endif

void svga_render_text_80(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->firstline_draw == 2000) 
                svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;
        
        if (svga->fullchange)
        {
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[32 + x_add];
                int x, xx;
                int drawcursor;
                uint8_t chr, attr, dat;
                uint32_t charaddr;
                int fg, bg;
                int xinc = (svga->seqregs[1] & 1) ? 8 : 9;

                for (x = 0; x < svga->hdisp; x += xinc)
                {
                        drawcursor = ((svga->ma == svga->ca) && svga->con && svga->cursoron);
			chr = svga->vram[svga->ma << 1];
			attr = svga->vram[(svga->ma << 1) + 1];
                        if (attr & 8) charaddr = svga->charsetb + (chr * 128);
                        else          charaddr = svga->charseta + (chr * 128);

                        if (drawcursor) 
                        { 
                                bg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask]; 
                                fg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask]; 
                        }
                        else
                        {
                                fg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask];
                                bg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask];
                                if (attr & 0x80 && svga->attrregs[0x10] & 8)
                                {
                                        bg = svga->pallook[svga->egapal[(attr >> 4) & 7] & svga->dac_mask];
                                        if (svga->blink & 16) 
                                                fg = bg;
                                }
                        }

                        dat = svga->vram[charaddr + (svga->sc << 2)];
                        if (svga->seqregs[1] & 1) 
                        { 
                                for (xx = 0; xx < 8; xx++) 
                                        p[xx] = (dat & (0x80 >> xx)) ? svga_color_transform(fg) : svga_color_transform(bg);
                        }
                        else
                        {
                                for (xx = 0; xx < 8; xx++) 
                                        p[xx] = (dat & (0x80 >> xx)) ? svga_color_transform(fg) : svga_color_transform(bg);
                                if ((chr & ~0x1F) != 0xC0 || !(svga->attrregs[0x10] & 4)) 
                                        p[8] = svga_color_transform(bg);
                                else                  
                                        p[8] = (dat & 1) ? svga_color_transform(fg) : svga_color_transform(bg);
                        }
                        svga->ma += 4; 
	                svga->ma = svga_mask_addr(svga->ma, svga);
                        p += xinc;
                }
        }
}

#if 0
void svga_render_text_80_12(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = (enable_overscan) ? 12 : 0;
	int dl = svga_display_line(svga);

        if (svga->firstline_draw == 2000) 
                svga->firstline_draw = svga->displine;
        svga->lastline_draw = svga->displine;
        
        if (svga->fullchange)
        {
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[32 + x_add];
                int x, xx;
                int drawcursor;
                uint8_t chr, attr;
		uint16_t dat;
                uint32_t charaddr;
                int fg, bg;
                int xinc = 12;

                for (x = 0; x < svga->hdisp; x += xinc)
                {
                        drawcursor = ((svga->ma == (svga->ca + 8)) && svga->con && svga->cursoron);
			chr = svga->vram[svga->ma << 1];
			attr = svga->vram[(svga->ma << 1) + 1];
                        if (attr & 8) charaddr = svga->charsetb + (chr * 128);
                        else          charaddr = svga->charseta + (chr * 128);

                        if (drawcursor) 
                        { 
                                bg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask]; 
                                fg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask]; 
                        }
                        else
                        {
                                fg = svga->pallook[svga->egapal[attr & 15] & svga->dac_mask];
                                bg = svga->pallook[svga->egapal[attr >> 4] & svga->dac_mask];
                                if (attr & 0x80 && svga->attrregs[0x10] & 8)
                                {
                                        bg = svga->pallook[svga->egapal[(attr >> 4) & 7] & svga->dac_mask];
                                        if (svga->blink & 16) 
                                                fg = bg;
                                }
                        }

                        dat = svga->vram[charaddr + (svga->sc << 2)] << 4;
                        dat |= ((svga->vram[charaddr + (svga->sc << 2) + 1]) >> 4);
			for (xx = 0; xx < 12; xx++) 
				p[xx] = (dat & (0x800 >> xx)) ? svga_color_transform(fg) : svga_color_transform(bg);
                        svga->ma += 4; 
	                svga->ma = svga_mask_addr(svga->ma, svga);
                        p += xinc;
                }
        }
}
#endif

void svga_render_2bpp_lowres(svga_t *svga)
{
        int changed_offset;
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);
        
        if (svga->sc & 1 && !(svga->crtc[0x17] & 1))
                changed_offset = (svga->ma << 1) >> 12;
        else
                changed_offset = ((svga->ma << 1) + 0x8000) >> 12;
                
        if (svga->changedvram[changed_offset] || svga->changedvram[changed_offset + 1] || svga->fullchange)
        {
                int x;
                int offset = ((8 - svga->scrollcache) << 1) + 16;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;
                       
                for (x = 0; x <= svga->hdisp; x += 16)
                {
                        uint8_t dat[2];
                        
                        if (svga->sc & 1 && !(svga->crtc[0x17] & 1))
                        {
                                dat[0] = svga->vram[(svga->ma << 1) + 0x8000];
                                dat[1] = svga->vram[(svga->ma << 1) + 0x8001];
                        }
                        else
                        {
                                dat[0] = svga->vram[(svga->ma << 1)];
                                dat[1] = svga->vram[(svga->ma << 1) + 1];
                        }
                        svga->ma += 4; 
                        svga->ma = svga_mask_addr(svga->ma, svga);

                        p[0]  = p[1]  = svga_color_transform(svga->pallook[svga->egapal[(dat[0] >> 6) & 3] & svga->dac_mask]);
                        p[2]  = p[3]  = svga_color_transform(svga->pallook[svga->egapal[(dat[0] >> 4) & 3] & svga->dac_mask]);
                        p[4]  = p[5]  = svga_color_transform(svga->pallook[svga->egapal[(dat[0] >> 2) & 3] & svga->dac_mask]);
                        p[6]  = p[7]  = svga_color_transform(svga->pallook[svga->egapal[dat[0] & 3] & svga->dac_mask]);
                        p[8]  = p[9]  = svga_color_transform(svga->pallook[svga->egapal[(dat[1] >> 6) & 3] & svga->dac_mask]);
                        p[10] = p[11] = svga_color_transform(svga->pallook[svga->egapal[(dat[1] >> 4) & 3] & svga->dac_mask]);
                        p[12] = p[13] = svga_color_transform(svga->pallook[svga->egapal[(dat[1] >> 2) & 3] & svga->dac_mask]);
                        p[14] = p[15] = svga_color_transform(svga->pallook[svga->egapal[dat[1] & 3] & svga->dac_mask]);

                        p += 16;
                }
        }
}

void svga_render_2bpp_highres(svga_t *svga)
{
        int changed_offset;
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->sc & 1 && !(svga->crtc[0x17] & 1))
                changed_offset = ((svga->ma << 1) | 0x8000) >> 12;
        else
                changed_offset = (svga->ma << 1) >> 12;
                                
        if (svga->changedvram[changed_offset] || svga->changedvram[changed_offset + 1] || svga->fullchange)
        {
                int x;
                int offset = (8 - svga->scrollcache) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;
                       
                for (x = 0; x <= svga->hdisp; x += 8)
                {
                        uint8_t dat[2];
                        
                        if (svga->sc & 1 && !(svga->crtc[0x17] & 1))
                        {
                                dat[0] = svga->vram[(svga->ma << 1) + 0x8000];
                                dat[1] = svga->vram[(svga->ma << 1) + 0x8001];
                        }
                        else
                        {
                                dat[0] = svga->vram[(svga->ma << 1)];
                                dat[1] = svga->vram[(svga->ma << 1) + 1];
                        }
                        svga->ma += 4; 
                        svga->ma = svga_mask_addr(svga->ma, svga);

                        p[0] = svga_color_transform(svga->pallook[svga->egapal[(dat[0] >> 6) & 3] & svga->dac_mask]);
                        p[1] = svga_color_transform(svga->pallook[svga->egapal[(dat[0] >> 4) & 3] & svga->dac_mask]);
                        p[2] = svga_color_transform(svga->pallook[svga->egapal[(dat[0] >> 2) & 3] & svga->dac_mask]);
                        p[3] = svga_color_transform(svga->pallook[svga->egapal[dat[0] & 3] & svga->dac_mask]);
                        p[4] = svga_color_transform(svga->pallook[svga->egapal[(dat[1] >> 6) & 3] & svga->dac_mask]);
                        p[5] = svga_color_transform(svga->pallook[svga->egapal[(dat[1] >> 4) & 3] & svga->dac_mask]);
                        p[6] = svga_color_transform(svga->pallook[svga->egapal[(dat[1] >> 2) & 3] & svga->dac_mask]);
                        p[7] = svga_color_transform(svga->pallook[svga->egapal[dat[1] & 3] & svga->dac_mask]);
                        
                        p += 8;
                }
        }
}

void svga_render_4bpp_lowres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[(svga->ma >> 12) + 1] || svga->fullchange)
        {
                int x;
                int offset = ((8 - svga->scrollcache) << 1) + 16;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x += 16)
                {
                        uint8_t edat[4];
                        uint8_t dat;

                        *(uint32_t *)(&edat[0]) = *(uint32_t *)(&svga->vram[svga->ma]);                        
                        svga->ma += 4; 
                        svga->ma = svga_mask_addr(svga->ma, svga);

                        dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
                        p[0]  = p[1]  = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[2]  = p[3]  = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                        dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
                        p[4]  = p[5]  = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[6]  = p[7]  = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                        dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
                        p[8]  = p[9]  = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[10] = p[11] = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                        dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
                        p[12] = p[13] = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[14] = p[15] = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                                                
                        p += 16;
                }
        }
}

void svga_render_4bpp_highres(svga_t *svga)
{
        int changed_offset;
	int changed_offset2;
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);
        
        if (svga->sc & 1 && !(svga->crtc[0x17] & 1))
                changed_offset = svga_mask_addr(svga->ma | 0x8000, svga) >> 12;
        else
                changed_offset = svga->ma >> 12;
                
        if (svga->sc & 1 && !(svga->crtc[0x17] & 1))
                changed_offset2 = svga_mask_addr((svga->ma | 0x8000) + 4096, svga) >> 12;
        else
                changed_offset2 = svga_mask_addr(svga->ma + 4096, svga) >> 12;
                
        if (svga->changedvram[changed_offset] || svga->changedvram[changed_offset2] || svga->fullchange)
        {
                int x;
                int offset = (8 - svga->scrollcache) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
        
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;
                
                for (x = 0; x <= svga->hdisp; x += 8)
                {
                        uint8_t edat[4];
                        uint8_t dat;

                        if (svga->sc & 1 && !(svga->crtc[0x17] & 1))                       
                                *(uint32_t *)(&edat[0]) = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma | 0x8000, svga)]);
                        else
                                *(uint32_t *)(&edat[0]) = *(uint32_t *)(&svga->vram[svga->ma]);
                        svga->ma += 4;
                        svga->ma = svga_mask_addr(svga->ma, svga);

                        dat = edatlookup[edat[0] >> 6][edat[1] >> 6] | (edatlookup[edat[2] >> 6][edat[3] >> 6] << 2);
                        p[0] = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[1] = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                        dat = edatlookup[(edat[0] >> 4) & 3][(edat[1] >> 4) & 3] | (edatlookup[(edat[2] >> 4) & 3][(edat[3] >> 4) & 3] << 2);
                        p[2] = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[3] = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                        dat = edatlookup[(edat[0] >> 2) & 3][(edat[1] >> 2) & 3] | (edatlookup[(edat[2] >> 2) & 3][(edat[3] >> 2) & 3] << 2);
                        p[4] = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[5] = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                        dat = edatlookup[edat[0] & 3][edat[1] & 3] | (edatlookup[edat[2] & 3][edat[3] & 3] << 2);
                        p[6] = svga_color_transform(svga->pallook[svga->egapal[(dat >> 4) & svga->plane_mask] & svga->dac_mask]);
                        p[7] = svga_color_transform(svga->pallook[svga->egapal[dat & svga->plane_mask] & svga->dac_mask]);
                        
                        p += 8;
                }
        }
}

void svga_render_8bpp_lowres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - (svga->scrollcache & 6)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;
                                                                
                for (x = 0; x <= svga->hdisp; x += 8)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga->ma]);
                        
                        p[0] = p[1] = svga_color_transform(svga->pallook[dat & svga->dac_mask]);
                        p[2] = p[3] = svga_color_transform(svga->pallook[(dat >> 8) & svga->dac_mask]);
                        p[4] = p[5] = svga_color_transform(svga->pallook[(dat >> 16) & svga->dac_mask]);
                        p[6] = p[7] = svga_color_transform(svga->pallook[(dat >> 24) & svga->dac_mask]);
                        
                        svga->ma += 4;
	                svga->ma = svga_mask_addr(svga->ma, svga);
                        p += 8;
                }
        }
}

void svga_render_8bpp_highres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];

                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;
                                                                
                for (x = 0; x <= svga->hdisp; x += 8)
                {
                        uint32_t dat;
                        dat = *(uint32_t *)(&svga->vram[svga->ma]);
                        p[0] = svga_color_transform(svga->pallook[dat & svga->dac_mask]);
                        p[1] = svga_color_transform(svga->pallook[(dat >> 8) & svga->dac_mask]);
                        p[2] = svga_color_transform(svga->pallook[(dat >> 16) & svga->dac_mask]);
                        p[3] = svga_color_transform(svga->pallook[(dat >> 24) & svga->dac_mask]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + 4, svga)]);
                        p[4] = svga_color_transform(svga->pallook[dat & svga->dac_mask]);
                        p[5] = svga_color_transform(svga->pallook[(dat >> 8) & svga->dac_mask]);
                        p[6] = svga_color_transform(svga->pallook[(dat >> 16) & svga->dac_mask]);
                        p[7] = svga_color_transform(svga->pallook[(dat >> 24) & svga->dac_mask]);
                        
                        svga->ma += 8;
	                svga->ma = svga_mask_addr(svga->ma, svga);
                        p += 8;
                }
        }
}

void svga_render_15bpp_lowres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - (svga->scrollcache & 6)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;
               
                for (x = 0; x <= svga->hdisp; x += 4)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1), svga)]);

                        p[x]     = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 1] = svga_color_transform(video_15to32[dat >> 16]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1) + 4, svga)]);

                        p[x]     = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 1] = svga_color_transform(video_15to32[dat >> 16]);
                }
                svga->ma += x << 1; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}

void svga_render_15bpp_highres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];

                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x += 8)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1), svga)]);
                        p[x]     = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 1] = svga_color_transform(video_15to32[dat >> 16]);
                        
                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1) + 4, svga)]);
                        p[x + 2] = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 3] = svga_color_transform(video_15to32[dat >> 16]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1) + 8, svga)]);
                        p[x + 4] = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 5] = svga_color_transform(video_15to32[dat >> 16]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1) + 12, svga)]);
                        p[x + 6] = svga_color_transform(video_15to32[dat & 0xffff]);
                        p[x + 7] = svga_color_transform(video_15to32[dat >> 16]);
                }
                svga->ma += x << 1; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}

void svga_render_16bpp_lowres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - (svga->scrollcache & 6)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;
               
                for (x = 0; x <= svga->hdisp; x += 4)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1), svga)]);

                        p[x]     = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 1] = svga_color_transform(video_16to32[dat >> 16]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1) + 4, svga)]);

                        p[x]     = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 1] = svga_color_transform(video_16to32[dat >> 16]);
                }
                svga->ma += x << 1; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}

void svga_render_16bpp_highres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];

                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x += 8)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1), svga)]);
                        p[x]     = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 1] = svga_color_transform(video_16to32[dat >> 16]);
                        
                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1) + 4, svga)]);
                        p[x + 2] = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 3] = svga_color_transform(video_16to32[dat >> 16]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1) + 8, svga)]);
                        p[x + 4] = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 5] = svga_color_transform(video_16to32[dat >> 16]);

                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 1) + 12, svga)]);
                        p[x + 6] = svga_color_transform(video_16to32[dat & 0xffff]);
                        p[x + 7] = svga_color_transform(video_16to32[dat >> 16]);
                }
                svga->ma += x << 1; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}

void svga_render_24bpp_lowres(svga_t *svga)
{
        int x, offset;
        uint32_t fg;
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);
        
        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                offset = (8 - (svga->scrollcache & 6)) + 24;

                for (x = 0; x <= svga->hdisp; x++)
                {
                        fg = svga->vram[svga->ma] | (svga->vram[svga_mask_addr(svga->ma + 1, svga)] << 8) | (svga->vram[svga_mask_addr(svga->ma + 2, svga)] << 16);
                        svga->ma += 3; 
                        svga->ma = svga_mask_addr(svga->ma, svga);
                        ((uint32_t *)buffer32->line[dl])[(x << 1) + offset + x_add] = ((uint32_t *)buffer32->line[dl])[(x << 1) + 1 + offset + x_add] = svga_color_transform(fg);
                }
        }
}

void svga_render_24bpp_highres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x += 4)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga->ma]);
                        p[x] = svga_color_transform(dat & 0xffffff);
                        
                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + 3, svga)]);
                        p[x + 1] = svga_color_transform(dat & 0xffffff);
                        
                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + 6, svga)]);
                        p[x + 2] = svga_color_transform(dat & 0xffffff);
                        
                        dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + 9, svga)]);
                        p[x + 3] = svga_color_transform(dat & 0xffffff);
                        
                        svga->ma += 12;
	                svga->ma = svga_mask_addr(svga->ma, svga);
                }
        }
}

void svga_render_32bpp_lowres(svga_t *svga)
{
        int x, offset;
        uint32_t fg;
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);
        
        if (svga->changedvram[svga->ma >> 12] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->fullchange)
        {
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                offset = (8 - (svga->scrollcache & 6)) + 24;

                for (x = 0; x <= svga->hdisp; x++)
                {
                        fg = svga->vram[svga->ma] | (svga->vram[svga_mask_addr(svga->ma + 1, svga)] << 8) | (svga->vram[svga_mask_addr(svga->ma + 2, svga)] << 16);
                        svga->ma += 4; 
                        svga->ma = svga_mask_addr(svga->ma, svga);
                        ((uint32_t *)buffer32->line[dl])[(x << 1) + offset + x_add] = ((uint32_t *)buffer32->line[dl])[(x << 1) + 1 + offset + x_add] = svga_color_transform(fg);
                }
        }
}

/*72%
  91%*/
void svga_render_32bpp_highres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] ||  svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 2, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x++)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 2), svga)]);
                        p[x] = svga_color_transform(dat & 0xffffff);
                }
                svga->ma += 4; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}

void svga_render_ABGR8888_highres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] ||  svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 2, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x++)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 2), svga)]);
                        p[x] = svga_color_transform(((dat & 0xff0000) >> 16) | (dat & 0x00ff00) | ((dat & 0x0000ff) << 16));
                }
                svga->ma += 4; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}

void svga_render_RGBA8888_highres(svga_t *svga)
{
	int y_add = (enable_overscan) ? 16 : 0;
	int x_add = y_add >> 1;
	int dl = svga_display_line(svga);

        if (svga->changedvram[svga->ma >> 12] ||  svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 1, svga)] || svga->changedvram[svga_mask_changedaddr((svga->ma >> 12) + 2, svga)] || svga->fullchange)
        {
                int x;
                int offset = (8 - ((svga->scrollcache & 6) >> 1)) + 24;
                uint32_t *p = &((uint32_t *)buffer32->line[dl])[offset + x_add];
                
                if (svga->firstline_draw == 2000) 
                        svga->firstline_draw = svga->displine;
                svga->lastline_draw = svga->displine;

                for (x = 0; x <= svga->hdisp; x++)
                {
                        uint32_t dat = *(uint32_t *)(&svga->vram[svga_mask_addr(svga->ma + (x << 2), svga)]);
                        p[x] = svga_color_transform(dat >> 8);
                }
                svga->ma += 4; 
                svga->ma = svga_mask_addr(svga->ma, svga);
        }
}
