/*
 * VARCem   Virtual ARchaeological Computer EMulator.
 *          An emulator of (mostly) x86-based PC systems and devices,
 *          using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *          spanning the era between 1981 and 1995.
 *
 *          Provide centralized access to the PNG image handler.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2018 Fred N. van Kempen.
 *
 *          Redistribution and  use  in source  and binary forms, with
 *          or  without modification, are permitted  provided that the
 *          following conditions are met:
 *
 *          1. Redistributions of  source  code must retain the entire
 *             above notice, this list of conditions and the following
 *             disclaimer.
 *
 *          2. Redistributions in binary form must reproduce the above
 *             copyright  notice,  this list  of  conditions  and  the
 *             following disclaimer in  the documentation and/or other
 *             materials provided with the distribution.
 *
 *          3. Neither the  name of the copyright holder nor the names
 *             of  its  contributors may be used to endorse or promote
 *             products  derived from  this  software without specific
 *             prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <errno.h>
#define SPNG_STATIC 1
#include <spng.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/ui.h>
#include <86box/video.h>
#include <86box/png_struct.h>

#ifdef ENABLE_PNG_LOG
int png_do_log = ENABLE_PNG_LOG;

static void
png_log(const char *fmt, ...)
{
    va_list ap;

    if (png_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define png_log(fmt, ...)
#endif

/* Write the given BITMAP-format image as an 8-bit RGBA PNG image file. */
void
png_write_rgb(char *fn, uint8_t *pix, int16_t w, int16_t h, uint16_t pitch, PALETTE palcol)
{
    spng_ctx               *png  = NULL;
    struct spng_ihdr        ihdr = {};
    struct spng_plte        plte = {};
    struct spng_plte_entry *palette = &plte.entries[0];
    FILE                   *fp;

    /* Create the image file. */
    fp = plat_fopen(fn, "wb");
    if (fp == NULL) {
        png_log("PNG: File %s could not be opened for writing!\n", fn);
error:
        if (png != NULL)
            spng_ctx_free(png);
        if (fp != NULL)
            (void) fclose(fp);
        return;
    }

    /* Initialize PNG stuff. */
    png = spng_ctx_new(SPNG_CTX_ENCODER);
    if (png == NULL) {
        png_log("PNG: spng_ctx_new failed!\n");
        goto error;
    }


    /* Finalize the initing of png library */
    spng_set_png_file(png, fp);
    spng_set_option(png, SPNG_IMG_COMPRESSION_LEVEL, 9);
    spng_set_option(png, SPNG_IMG_MEM_LEVEL, 8);
    spng_set_option(png, SPNG_IMG_WINDOW_BITS, 15);
    spng_set_option(png, SPNG_IMG_COMPRESSION_STRATEGY, 1);

    ihdr.bit_depth = 8;
    ihdr.width = w;
    ihdr.height = h;
    ihdr.color_type = SPNG_COLOR_TYPE_INDEXED;
    spng_set_ihdr(png, &ihdr);

    for (uint16_t i = 0; i < 256; i++) {
        palette[i].red   = palcol[i].r;
        palette[i].green = palcol[i].g;
        palette[i].blue  = palcol[i].b;
        palette[i].alpha = 255;
    }

    plte.n_entries = 256;
    spng_set_plte(png, &plte);
    
    spng_encode_image(png, NULL, 0, SPNG_FMT_PNG, SPNG_ENCODE_PROGRESSIVE | SPNG_ENCODE_FINALIZE);

    for (int16_t i = 0; i < h; i++) {
        if (spng_encode_row(png, (pix + (i * pitch)), w) == SPNG_EOI)
            break;
    }

    /* Clean up. */
    (void) fclose(fp);
    spng_ctx_free(png);
}
