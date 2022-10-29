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
#define PNG_DEBUG 0
#include <png.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/ui.h>
#include <86box/video.h>
#include <86box/png_struct.h>

#ifdef _WIN32
#    define PATH_PNG_DLL "libpng16-16.dll"
#elif defined __APPLE__
#    define PATH_PNG_DLL "libpng16.dylib"
#else
#    define PATH_PNG_DLL "libpng16.so"
#endif

#ifndef PNG_Z_DEFAULT_STRATEGY
#    define PNG_Z_DEFAULT_STRATEGY 1
#endif

#define PNGFUNC(x) png_##x

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

static void
error_handler(png_structp arg, const char *str)
{
    png_log("PNG: stream 0x%08lx error '%s'\n", arg, str);
}

static void
warning_handler(png_structp arg, const char *str)
{
    png_log("PNG: stream 0x%08lx warning '%s'\n", arg, str);
}

/* Write the given image as an 8-bit GrayScale PNG image file. */
int
png_write_gray(char *fn, int inv, uint8_t *pix, int16_t w, int16_t h)
{
    png_structp png  = NULL;
    png_infop   info = NULL;
    png_bytep   row;
    int16_t     x, y;
    FILE       *fp;

    /* Create the image file. */
    fp = plat_fopen(fn, "wb");
    if (fp == NULL) {
        /* Yes, this looks weird. */
        if (fp == NULL)
            png_log("PNG: file %s could not be opened for writing!\n", fn);
        else
error:
            png_log("PNG: fatal error, bailing out, error = %i\n", errno);
        if (png != NULL)
            PNGFUNC(destroy_write_struct)
            (&png, &info);
        if (fp != NULL)
            (void) fclose(fp);
        return (0);
    }

    /* Initialize PNG stuff. */
    png = PNGFUNC(create_write_struct)(PNG_LIBPNG_VER_STRING, NULL,
                                       error_handler, warning_handler);
    if (png == NULL) {
        png_log("PNG: create_write_struct failed!\n");
        goto error;
    }

    info = PNGFUNC(create_info_struct)(png);
    if (info == NULL) {
        png_log("PNG: create_info_struct failed!\n");
        goto error;
    }

    PNGFUNC(init_io)
    (png, fp);

    PNGFUNC(set_IHDR)
    (png, info, w, h, 8, PNG_COLOR_TYPE_GRAY,
     PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
     PNG_FILTER_TYPE_DEFAULT);

    PNGFUNC(write_info)
    (png, info);

    /* Create a buffer for one scanline of pixels. */
    row = (png_bytep) malloc(PNGFUNC(get_rowbytes)(png, info));

    /* Process all scanlines in the image. */
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            /* Copy the pixel data. */
            if (inv)
                row[x] = 255 - pix[(y * w) + x];
            else
                row[x] = pix[(y * w) + x];
        }

        /* Write image to the file. */
        PNGFUNC(write_rows)
        (png, &row, 1);
    }

    /* No longer need the row buffer. */
    free(row);

    PNGFUNC(write_end)
    (png, NULL);

    PNGFUNC(destroy_write_struct)
    (&png, &info);

    /* Clean up. */
    (void) fclose(fp);

    return (1);
}

/* Write the given BITMAP-format image as an 8-bit RGBA PNG image file. */
void
png_write_rgb(char *fn, uint8_t *pix, int16_t w, int16_t h, uint16_t pitch, PALETTE palcol)
{
    png_structp png  = NULL;
    png_infop   info = NULL;
    png_bytep  *rows;
    png_color   palette[256];
    FILE       *fp;
    int         i;

    /* Create the image file. */
    fp = plat_fopen(fn, "wb");
    if (fp == NULL) {
        png_log("PNG: File %s could not be opened for writing!\n", fn);
error:
        if (png != NULL)
            PNGFUNC(destroy_write_struct)
            (&png, &info);
        if (fp != NULL)
            (void) fclose(fp);
        return;
    }

    /* Initialize PNG stuff. */
    png = PNGFUNC(create_write_struct)(PNG_LIBPNG_VER_STRING, NULL,
                                       error_handler, warning_handler);
    if (png == NULL) {
        png_log("PNG: create_write_struct failed!\n");
        goto error;
    }

    info = PNGFUNC(create_info_struct)(png);
    if (info == NULL) {
        png_log("PNG: create_info_struct failed!\n");
        goto error;
    }

    /* Finalize the initing of png library */
    PNGFUNC(init_io)
    (png, fp);
    PNGFUNC(set_compression_level)
    (png, 9);

    /* set other zlib parameters */
    PNGFUNC(set_compression_mem_level)
    (png, 8);
    PNGFUNC(set_compression_strategy)
    (png, PNG_Z_DEFAULT_STRATEGY);
    PNGFUNC(set_compression_window_bits)
    (png, 15);
    PNGFUNC(set_compression_method)
    (png, 8);
    PNGFUNC(set_compression_buffer_size)
    (png, 8192);

    PNGFUNC(set_IHDR)
    (png, info, w, h, 8, PNG_COLOR_TYPE_PALETTE,
     PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
     PNG_FILTER_TYPE_DEFAULT);

    for (i = 0; i < 256; i++) {
        palette[i].red   = palcol[i].r;
        palette[i].green = palcol[i].g;
        palette[i].blue  = palcol[i].b;
    }

    PNGFUNC(set_PLTE)
    (png, info, palette, 256);

    /* Create a buffer for scanlines of pixels. */
    rows = (png_bytep *) malloc(sizeof(png_bytep) * h);
    for (i = 0; i < h; i++) {
        /* Create a buffer for this scanline. */
        rows[i] = (pix + (i * pitch));
    }

    PNGFUNC(set_rows)
    (png, info, rows);

    PNGFUNC(write_png)
    (png, info, 0, NULL);

    /* Clean up. */
    (void) fclose(fp);

    PNGFUNC(destroy_write_struct)
    (&png, &info);

    /* No longer need the row buffers. */
    free(rows);
}
