/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		3DFX Voodoo emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *
 *		Copyright 2008-2020 Sarah Walker.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <wchar.h>
#include <math.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_voodoo_common.h>
#include <86box/vid_voodoo_display.h>
#include <86box/vid_voodoo_regs.h>
#include <86box/vid_voodoo_render.h>


#ifdef ENABLE_VOODOODISP_LOG
int voodoodisp_do_log = ENABLE_VOODOODISP_LOG;


static void
voodoodisp_log(const char *fmt, ...)
{
    va_list ap;

    if (voodoodisp_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define voodoodisp_log(fmt, ...)
#endif

void voodoo_update_ncc(voodoo_t *voodoo, int tmu)
{
        int tbl;

        for (tbl = 0; tbl < 2; tbl++)
        {
                int col;

                for (col = 0; col < 256; col++)
                {
                        int y = (col >> 4), i = (col >> 2) & 3, q = col & 3;
                        int i_r, i_g, i_b;
                        int q_r, q_g, q_b;

                        y = (voodoo->nccTable[tmu][tbl].y[y >> 2] >> ((y & 3) * 8)) & 0xff;

                        i_r = (voodoo->nccTable[tmu][tbl].i[i] >> 18) & 0x1ff;
                        if (i_r & 0x100)
                                i_r |= 0xfffffe00;
                        i_g = (voodoo->nccTable[tmu][tbl].i[i] >> 9) & 0x1ff;
                        if (i_g & 0x100)
                                i_g |= 0xfffffe00;
                        i_b = voodoo->nccTable[tmu][tbl].i[i] & 0x1ff;
                        if (i_b & 0x100)
                                i_b |= 0xfffffe00;

                        q_r = (voodoo->nccTable[tmu][tbl].q[q] >> 18) & 0x1ff;
                        if (q_r & 0x100)
                                q_r |= 0xfffffe00;
                        q_g = (voodoo->nccTable[tmu][tbl].q[q] >> 9) & 0x1ff;
                        if (q_g & 0x100)
                                q_g |= 0xfffffe00;
                        q_b = voodoo->nccTable[tmu][tbl].q[q] & 0x1ff;
                        if (q_b & 0x100)
                                q_b |= 0xfffffe00;

                        voodoo->ncc_lookup[tmu][tbl][col].rgba.r = CLAMP(y + i_r + q_r);
                        voodoo->ncc_lookup[tmu][tbl][col].rgba.g = CLAMP(y + i_g + q_g);
                        voodoo->ncc_lookup[tmu][tbl][col].rgba.b = CLAMP(y + i_b + q_b);
                        voodoo->ncc_lookup[tmu][tbl][col].rgba.a = 0xff;
                }
        }
}

void voodoo_pixelclock_update(voodoo_t *voodoo)
{
        int m  =  (voodoo->dac_pll_regs[0] & 0x7f) + 2;
        int n1 = ((voodoo->dac_pll_regs[0] >>  8) & 0x1f) + 2;
        int n2 = ((voodoo->dac_pll_regs[0] >> 13) & 0x07);
        float t = (14318184.0 * ((float)m / (float)n1)) / (float)(1 << n2);
        double clock_const;
        int line_length;

        if ((voodoo->dac_data[6] & 0xf0) == 0x20 ||
            (voodoo->dac_data[6] & 0xf0) == 0x60 ||
            (voodoo->dac_data[6] & 0xf0) == 0x70)
                t /= 2.0f;

        line_length = (voodoo->hSync & 0xff) + ((voodoo->hSync >> 16) & 0x3ff);

//        voodoodisp_log("Pixel clock %f MHz hsync %08x line_length %d\n", t, voodoo->hSync, line_length);

        voodoo->pixel_clock = t;

        clock_const = cpuclock / t;
        voodoo->line_time = (uint64_t)((double)line_length * clock_const * (double)(1ull << 32));
}

static void voodoo_calc_clutData(voodoo_t *voodoo)
{
        int c;

        for (c = 0; c < 256; c++)
        {
                voodoo->clutData256[c].r = (voodoo->clutData[c >> 3].r*(8-(c & 7)) +
                                           voodoo->clutData[(c >> 3)+1].r*(c & 7)) >> 3;
                voodoo->clutData256[c].g = (voodoo->clutData[c >> 3].g*(8-(c & 7)) +
                                           voodoo->clutData[(c >> 3)+1].g*(c & 7)) >> 3;
                voodoo->clutData256[c].b = (voodoo->clutData[c >> 3].b*(8-(c & 7)) +
                                           voodoo->clutData[(c >> 3)+1].b*(c & 7)) >> 3;
        }

        for (c = 0; c < 65536; c++)
        {
                int r = (c >> 8) & 0xf8;
                int g = (c >> 3) & 0xfc;
                int b = (c << 3) & 0xf8;
//                r |= (r >> 5);
//                g |= (g >> 6);
//                b |= (b >> 5);

                voodoo->video_16to32[c] = (voodoo->clutData256[r].r << 16) | (voodoo->clutData256[g].g << 8) | voodoo->clutData256[b].b;
        }
}



#define FILTDIV 256

static int FILTCAP, FILTCAPG, FILTCAPB = 0;	/* color filter threshold values */

void voodoo_generate_filter_v1(voodoo_t *voodoo)
{
        int g, h;
        float difference, diffg, diffb;
        float thiscol, thiscolg, thiscolb, lined;
	float fcr, fcg, fcb;

	fcr = FILTCAP * 5;
	fcg = FILTCAPG * 6;
	fcb = FILTCAPB * 5;

        for (g=0;g<FILTDIV;g++)         // pixel 1
        {
                for (h=0;h<FILTDIV;h++)      // pixel 2
                {
                        difference = (float)(h - g);
                        diffg = difference;
                        diffb = difference;

			thiscol = thiscolg = thiscolb = g;

                        if (difference > FILTCAP)
                                difference = FILTCAP;
                        if (difference < -FILTCAP)
                                difference = -FILTCAP;

                        if (diffg > FILTCAPG)
                                diffg = FILTCAPG;
                        if (diffg < -FILTCAPG)
                                diffg = -FILTCAPG;

                        if (diffb > FILTCAPB)
                                diffb = FILTCAPB;
                        if (diffb < -FILTCAPB)
                                diffb = -FILTCAPB;

			// hack - to make it not bleed onto black
			//if (g == 0){
			//difference = diffg = diffb = 0;
			//}

			if ((difference < fcr) || (-difference > -fcr))
        			thiscol =  g + (difference / 2);
			if ((diffg < fcg) || (-diffg > -fcg))
        			thiscolg =  g + (diffg / 2);		/* need these divides so we can actually undither! */
			if ((diffb < fcb) || (-diffb > -fcb))
        			thiscolb =  g + (diffb / 2);

                        if (thiscol < 0)
                                thiscol = 0;
                        if (thiscol > FILTDIV-1)
                                thiscol = FILTDIV-1;

                        if (thiscolg < 0)
                                thiscolg = 0;
                        if (thiscolg > FILTDIV-1)
                                thiscolg = FILTDIV-1;

                        if (thiscolb < 0)
                                thiscolb = 0;
                        if (thiscolb > FILTDIV-1)
                                thiscolb = FILTDIV-1;

                        voodoo->thefilter[g][h] = thiscol;
                        voodoo->thefilterg[g][h] = thiscolg;
                        voodoo->thefilterb[g][h] = thiscolb;
                }

                lined = g + 4;
                if (lined > 255)
                        lined = 255;
                voodoo->purpleline[g][0] = lined;
                voodoo->purpleline[g][2] = lined;

                lined = g + 0;
                if (lined > 255)
                        lined = 255;
                voodoo->purpleline[g][1] = lined;
        }
}

void voodoo_generate_filter_v2(voodoo_t *voodoo)
{
        int g, h;
        float difference;
        float thiscol, thiscolg, thiscolb;
	float clr, clg, clb = 0;
	float fcr, fcg, fcb = 0;

	// pre-clamping

	fcr = FILTCAP;
	fcg = FILTCAPG;
	fcb = FILTCAPB;

	if (fcr > 32) fcr = 32;
	if (fcg > 32) fcg = 32;
	if (fcb > 32) fcb = 32;

        for (g=0;g<256;g++)         	// pixel 1 - our target pixel we want to bleed into
        {
		for (h=0;h<256;h++)      // pixel 2 - our main pixel
		{
			float avg;
			float avgdiff;

			difference = (float)(g - h);
			avg = (float)((g + g + g + g + h) / 5);
			avgdiff = avg - (float)((g + h + h + h + h) / 5);
			if (avgdiff < 0) avgdiff *= -1;
			if (difference < 0) difference *= -1;

			thiscol = thiscolg = thiscolb = g;

			// try lighten
			if (h > g)
			{
				clr = clg = clb = avgdiff;

				if (clr>fcr) clr=fcr;
                                if (clg>fcg) clg=fcg;
				if (clb>fcb) clb=fcb;


				thiscol = g + clr;
				thiscolg = g + clg;
				thiscolb = g + clb;

				if (thiscol>g+FILTCAP)
					thiscol=g+FILTCAP;
				if (thiscolg>g+FILTCAPG)
					thiscolg=g+FILTCAPG;
				if (thiscolb>g+FILTCAPB)
					thiscolb=g+FILTCAPB;


				if (thiscol>g+avgdiff)
					thiscol=g+avgdiff;
				if (thiscolg>g+avgdiff)
					thiscolg=g+avgdiff;
				if (thiscolb>g+avgdiff)
					thiscolb=g+avgdiff;

			}

			if (difference > FILTCAP)
				thiscol = g;
			if (difference > FILTCAPG)
				thiscolg = g;
			if (difference > FILTCAPB)
				thiscolb = g;

			// clamp
			if (thiscol < 0) thiscol = 0;
			if (thiscolg < 0) thiscolg = 0;
			if (thiscolb < 0) thiscolb = 0;

			if (thiscol > 255) thiscol = 255;
			if (thiscolg > 255) thiscolg = 255;
			if (thiscolb > 255) thiscolb = 255;

			// add to the table
			voodoo->thefilter[g][h] = (thiscol);
			voodoo->thefilterg[g][h] = (thiscolg);
			voodoo->thefilterb[g][h] = (thiscolb);

			// debug the ones that don't give us much of a difference
			//if (difference < FILTCAP)
			//voodoodisp_log("Voodoofilter: %ix%i - %f difference, %f average difference, R=%f, G=%f, B=%f\n", g, h, difference, avgdiff, thiscol, thiscolg, thiscolb);
                }

        }
}

void voodoo_threshold_check(voodoo_t *voodoo)
{
	int r, g, b;

	if (!voodoo->scrfilterEnabled)
		return;	/* considered disabled; don't check and generate */

	/* Check for changes, to generate anew table */
	if (voodoo->scrfilterThreshold != voodoo->scrfilterThresholdOld)
	{
		r = (voodoo->scrfilterThreshold >> 16) & 0xFF;
		g = (voodoo->scrfilterThreshold >> 8 ) & 0xFF;
		b = voodoo->scrfilterThreshold & 0xFF;

		FILTCAP = r;
		FILTCAPG = g;
		FILTCAPB = b;

		voodoodisp_log("Voodoo Filter Threshold Check: %06x - RED %i GREEN %i BLUE %i\n", voodoo->scrfilterThreshold, r, g, b);

		voodoo->scrfilterThresholdOld = voodoo->scrfilterThreshold;

		if (voodoo->type == VOODOO_2)
			voodoo_generate_filter_v2(voodoo);
		else
			voodoo_generate_filter_v1(voodoo);

		if (voodoo->type >= VOODOO_BANSHEE)
			voodoo_generate_vb_filters(voodoo, FILTCAP, FILTCAPG);
	}
}

static void voodoo_filterline_v1(voodoo_t *voodoo, uint8_t *fil, int column, uint16_t *src, int line)
{
	int x;

	// Scratchpad for avoiding feedback streaks
        uint8_t *fil3 = malloc((voodoo->h_disp) * 3);

	/* 16 to 32-bit */
        for (x=0; x<column;x++)
        {
		fil[x*3] 	=	((src[x] & 31) << 3);
		fil[x*3+1] 	=	(((src[x] >> 5) & 63) << 2);
 		fil[x*3+2] 	=	(((src[x] >> 11) & 31) << 3);

		// Copy to our scratchpads
 		fil3[x*3+0] 	= fil[x*3+0];
 		fil3[x*3+1] 	= fil[x*3+1];
 		fil3[x*3+2] 	= fil[x*3+2];
        }


        /* lines */

        if (line & 1)
        {
                for (x=0; x<column;x++)
                {
                        fil[x*3] = voodoo->purpleline[fil[x*3]][0];
                        fil[x*3+1] = voodoo->purpleline[fil[x*3+1]][1];
                        fil[x*3+2] = voodoo->purpleline[fil[x*3+2]][2];
                }
        }


        /* filtering time */

        for (x=1; x<column;x++)
        {
                fil3[(x)*3]   = voodoo->thefilterb[fil[x*3]][fil[	(x-1)		*3]];
                fil3[(x)*3+1] = voodoo->thefilterg[fil[x*3+1]][fil[	(x-1)		*3+1]];
                fil3[(x)*3+2] = voodoo->thefilter[fil[x*3+2]][fil[	(x-1)		*3+2]];
        }

        for (x=1; x<column;x++)
        {
                fil[(x)*3]   = voodoo->thefilterb[fil3[x*3]][fil3[	(x-1)		*3]];
                fil[(x)*3+1] = voodoo->thefilterg[fil3[x*3+1]][fil3[	(x-1)		*3+1]];
                fil[(x)*3+2] = voodoo->thefilter[fil3[x*3+2]][fil3[	(x-1)		*3+2]];
        }

        for (x=1; x<column;x++)
        {
                fil3[(x)*3]   = voodoo->thefilterb[fil[x*3]][fil[	(x-1)		*3]];
                fil3[(x)*3+1] = voodoo->thefilterg[fil[x*3+1]][fil[	(x-1)		*3+1]];
                fil3[(x)*3+2] = voodoo->thefilter[fil[x*3+2]][fil[	(x-1)		*3+2]];
        }

        for (x=0; x<column-1;x++)
        {
                fil[(x)*3]   = voodoo->thefilterb[fil3[x*3]][fil3[	(x+1)		*3]];
                fil[(x)*3+1] = voodoo->thefilterg[fil3[x*3+1]][fil3[	(x+1)		*3+1]];
                fil[(x)*3+2] = voodoo->thefilter[fil3[x*3+2]][fil3[	(x+1)		*3+2]];
        }

	free(fil3);
}


static void voodoo_filterline_v2(voodoo_t *voodoo, uint8_t *fil, int column, uint16_t *src, int line)
{
	int x;

	// Scratchpad for blending filter
        uint8_t *fil3 = malloc((voodoo->h_disp) * 3);

	/* 16 to 32-bit */
        for (x=0; x<column;x++)
        {
		// Blank scratchpads
 		fil3[x*3+0] = fil[x*3+0] =	((src[x] & 31) << 3);
 		fil3[x*3+1] = fil[x*3+1] =	(((src[x] >> 5) & 63) << 2);
 		fil3[x*3+2] = fil[x*3+2] =	(((src[x] >> 11) & 31) << 3);
        }

        /* filtering time */

	for (x=1; x<column-3;x++)
        {
		fil3[(x+3)*3]   = voodoo->thefilterb	[((src[x+3] & 31) << 3)]		[((src[x] & 31) << 3)];
		fil3[(x+3)*3+1] = voodoo->thefilterg	[(((src[x+3] >> 5) & 63) << 2)]		[(((src[x] >> 5) & 63) << 2)];
		fil3[(x+3)*3+2] = voodoo->thefilter	[(((src[x+3] >> 11) & 31) << 3)]	[(((src[x] >> 11) & 31) << 3)];

		fil[(x+2)*3]   = voodoo->thefilterb	[fil3[(x+2)*3]][((src[x] & 31) << 3)];
		fil[(x+2)*3+1] = voodoo->thefilterg	[fil3[(x+2)*3+1]][(((src[x] >> 5) & 63) << 2)];
		fil[(x+2)*3+2] = voodoo->thefilter	[fil3[(x+2)*3+2]][(((src[x] >> 11) & 31) << 3)];

		fil3[(x+1)*3]   = voodoo->thefilterb	[fil[(x+1)*3]][((src[x] & 31) << 3)];
		fil3[(x+1)*3+1] = voodoo->thefilterg	[fil[(x+1)*3+1]][(((src[x] >> 5) & 63) << 2)];
		fil3[(x+1)*3+2] = voodoo->thefilter	[fil[(x+1)*3+2]][(((src[x] >> 11) & 31) << 3)];

		fil[(x-1)*3]   = voodoo->thefilterb	[fil3[(x-1)*3]][((src[x] & 31) << 3)];
		fil[(x-1)*3+1] = voodoo->thefilterg	[fil3[(x-1)*3+1]][(((src[x] >> 5) & 63) << 2)];
		fil[(x-1)*3+2] = voodoo->thefilter	[fil3[(x-1)*3+2]][(((src[x] >> 11) & 31) << 3)];
        }

	// unroll for edge cases

	fil3[(column-3)*3]   = voodoo->thefilterb	[((src[column-3] & 31) << 3)]		[((src[column] & 31) << 3)];
	fil3[(column-3)*3+1] = voodoo->thefilterg	[(((src[column-3] >> 5) & 63) << 2)]	[(((src[column] >> 5) & 63) << 2)];
	fil3[(column-3)*3+2] = voodoo->thefilter	[(((src[column-3] >> 11) & 31) << 3)]	[(((src[column] >> 11) & 31) << 3)];

	fil3[(column-2)*3]   = voodoo->thefilterb	[((src[column-2] & 31) << 3)]		[((src[column] & 31) << 3)];
	fil3[(column-2)*3+1] = voodoo->thefilterg	[(((src[column-2] >> 5) & 63) << 2)]	[(((src[column] >> 5) & 63) << 2)];
	fil3[(column-2)*3+2] = voodoo->thefilter	[(((src[column-2] >> 11) & 31) << 3)]	[(((src[column] >> 11) & 31) << 3)];

	fil3[(column-1)*3]   = voodoo->thefilterb	[((src[column-1] & 31) << 3)]		[((src[column] & 31) << 3)];
	fil3[(column-1)*3+1] = voodoo->thefilterg	[(((src[column-1] >> 5) & 63) << 2)]	[(((src[column] >> 5) & 63) << 2)];
	fil3[(column-1)*3+2] = voodoo->thefilter	[(((src[column-1] >> 11) & 31) << 3)]	[(((src[column] >> 11) & 31) << 3)];

	fil[(column-2)*3]   = voodoo->thefilterb	[fil3[(column-2)*3]][((src[column] & 31) << 3)];
	fil[(column-2)*3+1] = voodoo->thefilterg	[fil3[(column-2)*3+1]][(((src[column] >> 5) & 63) << 2)];
	fil[(column-2)*3+2] = voodoo->thefilter		[fil3[(column-2)*3+2]][(((src[column] >> 11) & 31) << 3)];

	fil[(column-1)*3]   = voodoo->thefilterb	[fil3[(column-1)*3]][((src[column] & 31) << 3)];
	fil[(column-1)*3+1] = voodoo->thefilterg	[fil3[(column-1)*3+1]][(((src[column] >> 5) & 63) << 2)];
	fil[(column-1)*3+2] = voodoo->thefilter		[fil3[(column-1)*3+2]][(((src[column] >> 11) & 31) << 3)];

	fil3[(column-1)*3]   = voodoo->thefilterb	[fil[(column-1)*3]][((src[column] & 31) << 3)];
	fil3[(column-1)*3+1] = voodoo->thefilterg	[fil[(column-1)*3+1]][(((src[column] >> 5) & 63) << 2)];
	fil3[(column-1)*3+2] = voodoo->thefilter	[fil[(column-1)*3+2]][(((src[column] >> 11) & 31) << 3)];

	free(fil3);
}

void voodoo_callback(void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;

        if (voodoo->fbiInit0 & FBIINIT0_VGA_PASS)
        {
                if (voodoo->line < voodoo->v_disp)
                {
                        voodoo_t *draw_voodoo;
                        int draw_line;

                        if (SLI_ENABLED)
                        {
                                if (voodoo == voodoo->set->voodoos[1])
                                        goto skip_draw;

                                if (((voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) ? 1 : 0) == (voodoo->line & 1))
                                        draw_voodoo = voodoo;
                                else
                                        draw_voodoo = voodoo->set->voodoos[1];
                                draw_line = voodoo->line >> 1;
                        }
                        else
                        {
                                if (!(voodoo->fbiInit0 & 1))
                                        goto skip_draw;
                                draw_voodoo = voodoo;
                                draw_line = voodoo->line;
                        }

                        if (draw_voodoo->dirty_line[draw_line])
                        {
                                uint32_t *p = &buffer32->line[voodoo->line + 8][8];
                                uint16_t *src = (uint16_t *)&draw_voodoo->fb_mem[draw_voodoo->front_offset + draw_line*draw_voodoo->row_width];
                                int x;

                                draw_voodoo->dirty_line[draw_line] = 0;

                                if (voodoo->line < voodoo->dirty_line_low)
                                {
                                        voodoo->dirty_line_low = voodoo->line;
                                        video_wait_for_buffer();
                                }
                                if (voodoo->line > voodoo->dirty_line_high)
                                        voodoo->dirty_line_high = voodoo->line;

				/* Draw left overscan. */
				for (x = 0; x < 8; x++)
					buffer32->line[voodoo->line + 8][x] = 0x00000000;

                                if (voodoo->scrfilter && voodoo->scrfilterEnabled)
                                {
                                        uint8_t *fil = malloc((voodoo->h_disp) * 3);              /* interleaved 24-bit RGB */

                			if (voodoo->type == VOODOO_2)
	                                        voodoo_filterline_v2(voodoo, fil, voodoo->h_disp, src, voodoo->line);
					else
                                        	voodoo_filterline_v1(voodoo, fil, voodoo->h_disp, src, voodoo->line);

                                        for (x = 0; x < voodoo->h_disp; x++)
                                        {
                                                p[x] = (voodoo->clutData256[fil[x*3]].b << 0 | voodoo->clutData256[fil[x*3+1]].g << 8 | voodoo->clutData256[fil[x*3+2]].r << 16);
                                        }

					free(fil);
                                }
                                else
                                {
                                        for (x = 0; x < voodoo->h_disp; x++)
                                        {
                                                p[x] = draw_voodoo->video_16to32[src[x]];
                                        }
                                }

				/* Draw right overscan. */
				for (x = 0; x < 8; x++)
					buffer32->line[voodoo->line + 8][voodoo->h_disp + x + 8] = 0x00000000;
                        }
                }
        }
skip_draw:
        if (voodoo->line == voodoo->v_disp)
        {
//                voodoodisp_log("retrace %i %i %08x %i\n", voodoo->retrace_count, voodoo->swap_interval, voodoo->swap_offset, voodoo->swap_pending);
                voodoo->retrace_count++;
                if (SLI_ENABLED && (voodoo->fbiInit2 & FBIINIT2_SWAP_ALGORITHM_MASK) == FBIINIT2_SWAP_ALGORITHM_SLI_SYNC)
                {
                        if (voodoo == voodoo->set->voodoos[0])
                        {
                                voodoo_t *voodoo_1 = voodoo->set->voodoos[1];

                                thread_wait_mutex(voodoo->swap_mutex);
                                /*Only swap if both Voodoos are waiting for buffer swap*/
                                if (voodoo->swap_pending && (voodoo->retrace_count > voodoo->swap_interval) &&
                                    voodoo_1->swap_pending && (voodoo_1->retrace_count > voodoo_1->swap_interval))
                                {
                                        memset(voodoo->dirty_line, 1, 1024);
                                        voodoo->retrace_count = 0;
                                        voodoo->front_offset = voodoo->swap_offset;
                                        if (voodoo->swap_count > 0)
                                                voodoo->swap_count--;
                                        voodoo->swap_pending = 0;

                                        memset(voodoo_1->dirty_line, 1, 1024);
                                        voodoo_1->retrace_count = 0;
                                        voodoo_1->front_offset = voodoo_1->swap_offset;
                                        if (voodoo_1->swap_count > 0)
                                                voodoo_1->swap_count--;
                                        voodoo_1->swap_pending = 0;
                                        thread_release_mutex(voodoo->swap_mutex);

                                        thread_set_event(voodoo->wake_fifo_thread);
                                        thread_set_event(voodoo_1->wake_fifo_thread);

                                        voodoo->frame_count++;
                                        voodoo_1->frame_count++;
                                }
                                else
                                        thread_release_mutex(voodoo->swap_mutex);
                        }
                }
                else
                {
                        thread_wait_mutex(voodoo->swap_mutex);
                        if (voodoo->swap_pending && (voodoo->retrace_count > voodoo->swap_interval))
                        {
                                voodoo->front_offset = voodoo->swap_offset;
                                if (voodoo->swap_count > 0)
                                        voodoo->swap_count--;
                                voodoo->swap_pending = 0;
                                thread_release_mutex(voodoo->swap_mutex);

                                memset(voodoo->dirty_line, 1, 1024);
                                voodoo->retrace_count = 0;
                                thread_set_event(voodoo->wake_fifo_thread);
                                voodoo->frame_count++;
                        }
                        else
                                thread_release_mutex(voodoo->swap_mutex);
                }
                voodoo->v_retrace = 1;
        }
        voodoo->line++;

        if (voodoo->fbiInit0 & FBIINIT0_VGA_PASS)
        {
                if (voodoo->line == voodoo->v_disp)
                {
                        int force_blit = 0;
                        thread_wait_mutex(voodoo->force_blit_mutex);
                        if(voodoo->force_blit_count) {
                            force_blit = 1;
                            if(--voodoo->force_blit_count < 0)
                                voodoo->force_blit_count = 0;
                        }
                        thread_release_mutex(voodoo->force_blit_mutex);

                        if (voodoo->dirty_line_high > voodoo->dirty_line_low || force_blit)
                                svga_doblit(voodoo->h_disp, voodoo->v_disp-1, voodoo->svga);
                        if (voodoo->clutData_dirty)
                        {
                                voodoo->clutData_dirty = 0;
                                voodoo_calc_clutData(voodoo);
                        }
                        voodoo->dirty_line_high = -1;
                        voodoo->dirty_line_low = 2000;
                }
        }

        if (voodoo->line >= voodoo->v_total)
        {
                voodoo->line = 0;
                voodoo->v_retrace = 0;
        }
        if (voodoo->line_time)
		timer_advance_u64(&voodoo->timer, voodoo->line_time);
        else
		timer_advance_u64(&voodoo->timer, TIMER_USEC * 32);
}
