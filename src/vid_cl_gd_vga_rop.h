/*
 * QEMU Cirrus CLGD 54xx VGA Emulator.
 *
 * Copyright (c) 2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

inline void glue(rop_8_,ROP_NAME)(uint8_t *dst, uint8_t src)
{
    *dst = ROP_FN(*dst, src);
}

inline void glue(rop_16_,ROP_NAME)(uint16_t *dst, uint16_t src)
{
    *dst = ROP_FN(*dst, src);
}

inline void glue(rop_32_,ROP_NAME)(uint32_t *dst, uint32_t src)
{
    *dst = ROP_FN(*dst, src);
}

#define ROP_OP(d, s) glue(rop_8_,ROP_NAME)(d, s)
#define ROP_OP_16(d, s) glue(rop_16_,ROP_NAME)(d, s)
#define ROP_OP_32(d, s) glue(rop_32_,ROP_NAME)(d, s)
#undef ROP_FN

void
glue(cirrus_bitblt_rop_fwd_, ROP_NAME)(clgd_t *clgd, svga_t *svga,
                             uint8_t *dst,const uint8_t *src,
                             int dstpitch,int srcpitch,
                             int bltwidth,int bltheight)
{
    int x,y;
    dstpitch -= bltwidth;
    srcpitch -= bltwidth;

    if (bltheight > 1 && (dstpitch < 0 || srcpitch < 0)) {
        return;
    }

    for (y = 0; y < bltheight; y++) {
        for (x = 0; x < bltwidth; x++) {
            ROP_OP(dst, *src);
            dst++;
            src++;
        }
        dst += dstpitch;
        src += srcpitch;
    }
}

void
glue(cirrus_bitblt_rop_bkwd_, ROP_NAME)(clgd_t *clgd, svga_t *svga,
                                        uint8_t *dst,const uint8_t *src,
                                        int dstpitch,int srcpitch,
                                        int bltwidth,int bltheight)
{
    int x,y;
    dstpitch += bltwidth;
    srcpitch += bltwidth;
    for (y = 0; y < bltheight; y++) {
        for (x = 0; x < bltwidth; x++) {
            ROP_OP(dst, *src);
            dst--;
            src--;
        }
        dst += dstpitch;
        src += srcpitch;
    }
}

void
glue(glue(cirrus_bitblt_rop_fwd_transp_, ROP_NAME),_8)(clgd_t *clgd, svga_t *svga,
						       uint8_t *dst,const uint8_t *src,
						       int dstpitch,int srcpitch,
						       int bltwidth,int bltheight)
{
    int x,y;
    uint8_t p;
    dstpitch -= bltwidth;
    srcpitch -= bltwidth;
    for (y = 0; y < bltheight; y++) {
        for (x = 0; x < bltwidth; x++) {
	    p = *dst;
            ROP_OP(&p, *src);
	    if (p != svga->gdcreg[0x34]) *dst = p;
            dst++;
            src++;
        }
        dst += dstpitch;
        src += srcpitch;
    }
}

void
glue(glue(cirrus_bitblt_rop_bkwd_transp_, ROP_NAME),_8)(clgd_t *clgd, svga_t *svga,
							uint8_t *dst,const uint8_t *src,
							int dstpitch,int srcpitch,
							int bltwidth,int bltheight)
{
    int x,y;
    uint8_t p;
    dstpitch += bltwidth;
    srcpitch += bltwidth;
    for (y = 0; y < bltheight; y++) {
        for (x = 0; x < bltwidth; x++) {
	    p = *dst;
            ROP_OP(&p, *src);
	    if (p != svga->gdcreg[0x34]) *dst = p;
            dst--;
            src--;
        }
        dst += dstpitch;
        src += srcpitch;
    }
}

void
glue(glue(cirrus_bitblt_rop_fwd_transp_, ROP_NAME),_16)(clgd_t *clgd, svga_t *svga,
							uint8_t *dst,const uint8_t *src,
							int dstpitch,int srcpitch,
							int bltwidth,int bltheight)
{
    int x,y;
    uint8_t p1, p2;
    dstpitch -= bltwidth;
    srcpitch -= bltwidth;
    for (y = 0; y < bltheight; y++) {
        for (x = 0; x < bltwidth; x+=2) {
	    p1 = *dst;
	    p2 = *(dst+1);
            ROP_OP(&p1, *src);
            ROP_OP(&p2, *(src + 1));
	    if ((p1 != svga->gdcreg[0x34]) || (p2 != svga->gdcreg[0x35])) {
		*dst = p1;
		*(dst+1) = p2;
	    }
            dst+=2;
            src+=2;
        }
        dst += dstpitch;
        src += srcpitch;
    }
}

void
glue(glue(cirrus_bitblt_rop_bkwd_transp_, ROP_NAME),_16)(clgd_t *clgd, svga_t *svga,
							 uint8_t *dst,const uint8_t *src,
							 int dstpitch,int srcpitch,
							 int bltwidth,int bltheight)
{
    int x,y;
    uint8_t p1, p2;
    dstpitch += bltwidth;
    srcpitch += bltwidth;
    for (y = 0; y < bltheight; y++) {
        for (x = 0; x < bltwidth; x+=2) {
	    p1 = *(dst-1);
	    p2 = *dst;
            ROP_OP(&p1, *(src - 1));
            ROP_OP(&p2, *src);
	    if ((p1 != svga->gdcreg[0x34]) || (p2 != svga->gdcreg[0x35])) {
		*(dst-1) = p1;
		*dst = p2;
	    }
            dst-=2;
            src-=2;
        }
        dst += dstpitch;
        src += srcpitch;
    }
}

#define DEPTH 8
#include "vid_cl_gd_vga_rop2.h"

#define DEPTH 16
#include "vid_cl_gd_vga_rop2.h"

#define DEPTH 24
#include "vid_cl_gd_vga_rop2.h"

#define DEPTH 32
#include "vid_cl_gd_vga_rop2.h"

#undef ROP_NAME
#undef ROP_OP
#undef ROP_OP_16
#undef ROP_OP_32
