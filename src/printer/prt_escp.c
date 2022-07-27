/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of the Generic ESC/P Dot-Matrix printer.
 *
 *
 *
 * Authors:	Michael Drüing, <michael@drueing.de>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Based on code by Frederic Weymann (originally for DosBox.)
 *
 *		Copyright 2018,2019 Michael Drüing.
 *		Copyright 2019,2019 Fred N. van Kempen.
 *
 *		Redistribution and  use  in source  and binary forms, with
 *		or  without modification, are permitted  provided that the
 *		following conditions are met:
 *
 *		1. Redistributions of  source  code must retain the entire
 *		   above notice, this list of conditions and the following
 *		   disclaimer.
 *
 *		2. Redistributions in binary form must reproduce the above
 *		   copyright  notice,  this list  of  conditions  and  the
 *		   following disclaimer in  the documentation and/or other
 *		   materials provided with the distribution.
 *
 *		3. Neither the  name of the copyright holder nor the names
 *		   of  its  contributors may be used to endorse or promote
 *		   products  derived from  this  software without specific
 *		   prior written permission.
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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/pit.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/ui.h>
#include <86box/lpt.h>
#include <86box/video.h>
#include <86box/png_struct.h>
#include <86box/printer.h>
#include <86box/prt_devs.h>


/* Default page values (for now.) */
#define COLOR_BLACK 	7<<5
#define PAGE_WIDTH	8.5			/* standard U.S. Letter */
#define PAGE_HEIGHT	11.0
#define PAGE_LMARGIN	0.0
#define PAGE_RMARGIN	PAGE_WIDTH
#define PAGE_TMARGIN	0.0
#define PAGE_BMARGIN	PAGE_HEIGHT
#define PAGE_DPI	360
#define PAGE_CPI	10.0			/* standard 10 cpi */
#define PAGE_LPI	6.0			/* standard 6 lpi */


#ifdef _WIN32
# define PATH_FREETYPE_DLL	"freetype.dll"
#elif defined __APPLE__
# define PATH_FREETYPE_DLL	"libfreetype.dylib"
#else
# define PATH_FREETYPE_DLL	"libfreetype.so.6"
#endif


/* FreeType library handles - global so they can be shared. */
FT_Library	ft_lib = NULL;
void		*ft_handle = NULL;

static int	(*ft_Init_FreeType)(FT_Library *alibrary);
static int	(*ft_Done_Face)(FT_Face face);
static int	(*ft_New_Face)(FT_Library library, const char *filepathname,
			       FT_Long face_index, FT_Face *aface);
static int	(*ft_Set_Char_Size)(FT_Face face, FT_F26Dot6 char_width,
				    FT_F26Dot6 char_height,
				    FT_UInt horz_resolution,
				    FT_UInt vert_resolution);
static int	(*ft_Set_Transform)(FT_Face face, FT_Matrix *matrix,
				    FT_Vector *delta);
static int	(*ft_Get_Char_Index)(FT_Face face, FT_ULong charcode);
static int	(*ft_Load_Glyph)(FT_Face face, FT_UInt glyph_index,
				 FT_Int32 load_flags);
static int	(*ft_Render_Glyph)(FT_GlyphSlot slot,
				   FT_Render_Mode render_mode);


static dllimp_t ft_imports[] = {
  { "FT_Init_FreeType",		&ft_Init_FreeType	},
  { "FT_New_Face",		&ft_New_Face		},
  { "FT_Done_Face",		&ft_Done_Face		},
  { "FT_Set_Char_Size",		&ft_Set_Char_Size	},
  { "FT_Set_Transform",		&ft_Set_Transform	},
  { "FT_Get_Char_Index",	&ft_Get_Char_Index	},
  { "FT_Load_Glyph",		&ft_Load_Glyph		},
  { "FT_Render_Glyph",		&ft_Render_Glyph	},
  { NULL,			NULL			}
};


/* The fonts. */
#define FONT_DEFAULT		0
#define FONT_ROMAN		1
#define FONT_SANSSERIF		2
#define FONT_COURIER		3
#define FONT_SCRIPT		4
#define FONT_OCRA		5
#define FONT_OCRB		6

/* Font styles. */
#define STYLE_PROP		0x0001
#define STYLE_CONDENSED		0x0002
#define STYLE_BOLD		0x0004
#define STYLE_DOUBLESTRIKE	0x0008
#define STYLE_DOUBLEWIDTH	0x0010
#define STYLE_ITALICS		0x0020
#define STYLE_UNDERLINE		0x0040
#define STYLE_SUPERSCRIPT	0x0080
#define STYLE_SUBSCRIPT		0x0100
#define STYLE_STRIKETHROUGH	0x0200
#define STYLE_OVERSCORE		0x0400
#define STYLE_DOUBLEWIDTHONELINE 0x0800
#define STYLE_DOUBLEHEIGHT	0x1000

/* Underlining styles. */
#define SCORE_NONE		0x00
#define SCORE_SINGLE		0x01
#define SCORE_DOUBLE		0x02
#define SCORE_SINGLEBROKEN	0x05
#define SCORE_DOUBLEBROKEN	0x06

/* Print quality. */
#define QUALITY_DRAFT		0x01
#define QUALITY_LQ		0x02

/* Typefaces. */
#define TYPEFACE_ROMAN		0
#define TYPEFACE_SANSSERIF	1
#define TYPEFACE_COURIER	2
#define TYPEFACE_PRESTIGE	3
#define TYPEFACE_SCRIPT		4
#define TYPEFACE_OCRB		5
#define TYPEFACE_OCRA		6
#define TYPEFACE_ORATOR		7
#define TYPEFACE_ORATORS	8
#define TYPEFACE_SCRIPTC	9
#define TYPEFACE_ROMANT		10
#define TYPEFACE_SANSSERIFH	11
#define TYPEFACE_SVBUSABA	30
#define TYPEFACE_SVJITTRA	31


/* Some helper macros. */
#define PARAM16(x)		(dev->esc_parms[x+1] * 256 + dev->esc_parms[x])
#define PIXX			((unsigned)floor(dev->curr_x * dev->dpi + 0.5))
#define PIXY			((unsigned)floor(dev->curr_y * dev->dpi + 0.5))


typedef struct {
    int8_t	dirty;		/* has the page been printed on? */
    char	pad;

    uint16_t	w;		/* size and pitch //INFO */
    uint16_t	h;
    uint16_t	pitch;

    uint8_t	*pixels;	/* grayscale pixel data */
} psurface_t;


typedef struct {
    const char	*name;

    void	*lpt;

    pc_timer_t	pulse_timer;
    pc_timer_t	timeout_timer;

    char	page_fn[260];
    uint8_t 	color;

    /* page data (TODO: make configurable) */
    double	page_width,	/* all in inches */
		page_height,
		left_margin,
		top_margin,
		right_margin,
		bottom_margin;
    uint16_t	dpi;
    double	cpi;		/* defined chars per inch */
    double	lpi;		/* defined lines per inch */

    /* font data */
    double	actual_cpi;	/* actual cpi as with current font */
    double	linespacing;	/* in inch */
    double	hmi;		/* hor. motion index (inch); overrides CPI */

    /* tabstops */
    double	horizontal_tabs[32];
    uint8_t	num_horizontal_tabs;
    double	vertical_tabs[16];
    int8_t	num_vertical_tabs;

    /* bit graphics data */
    uint16_t	bg_h_density;		/* in dpi */
    uint16_t	bg_v_density;		/* in dpi */
    int8_t	bg_adjacent;		/* print adjacent pixels (ignored) */
    uint8_t	bg_bytes_per_column;
    uint16_t	bg_remaining_bytes;	/* #bytes left before img is complete */
    uint8_t	bg_column[6];		/* #bytes of the current and last col */
    uint8_t	bg_bytes_read;		/* #bytes read so far for current col */

    /* handshake data */
    uint8_t	data;
    uint8_t	ack;
    uint8_t	select;
    uint8_t	busy;
    uint8_t	int_pending;
    uint8_t	error;
    uint8_t	autofeed;

    /* ESC command data */
    int8_t	esc_seen;		/* set to 1 if an ESC char was seen */
    int8_t	fss_seen;
    uint16_t	esc_pending;		/* in which ESC command are we */
    uint8_t	esc_parms_req;
    uint8_t	esc_parms_curr;
    uint8_t	esc_parms[20];		/* 20 should be enough for everybody */

    /* internal page data */
    char	fontpath[1024];
    char	pagepath[1024];
    psurface_t	*page;
    double	curr_x, curr_y;		/* print head position (inch) */
    uint16_t	current_font;
    FT_Face	fontface;
    int8_t	lq_typeface;
    uint16_t	font_style;
    uint8_t	print_quality;
    uint8_t	font_score;
    double	extra_intra_space;	/* extra spacing between chars (inch) */

    /* other internal data */
    uint16_t	char_tables[4];		/* the character tables for ESC t */
    uint8_t	curr_char_table;	/* the active char table index */
    uint16_t	curr_cpmap[256];	/* current ASCII->Unicode map table */

    int8_t	multipoint_mode;	/* multipoint mode, ESC X */
    double	multipoint_size;	/* size of font, in points */
    double	multipoint_cpi;		/* chars per inch in multipoint mode */

    uint8_t	density_k;		/* density modes for ESC K/L/Y/Z */
    uint8_t	density_l;
    uint8_t	density_y;
    uint8_t	density_z;

    int8_t	print_upper_control;	/* ESC 6, ESC 7 */
    int8_t	print_everything_count;	/* for ESC ( ^ */

    double	defined_unit;		/* internal unit for some ESC/P
					 * commands. -1 = use default */

    uint8_t	msb;			/* MSB mode, -1 = off */
    uint8_t	ctrl;

    PALETTE	palcol;
} escp_t;


static void
update_font(escp_t *dev);
static void
blit_glyph(escp_t *dev, unsigned destx, unsigned desty, int8_t add);
static void
draw_hline(escp_t *dev, unsigned from_x, unsigned to_x, unsigned y, int8_t broken);
static void
init_codepage(escp_t *dev, uint16_t num);
static void
reset_printer(escp_t *dev);
static void
setup_bit_image(escp_t *dev, uint8_t density, uint16_t num_columns);
static void
print_bit_graph(escp_t *dev, uint8_t ch);
static void
new_page(escp_t *dev, int8_t save, int8_t resetx);


/* Codepage table, needed for ESC t ( */
static const uint16_t codepages[15] = {
      0, 437, 932, 850, 851, 853, 855, 860,
    863, 865, 852, 857, 862, 864, 866
};


/* "patches" to the codepage for the international charsets
 * these bytes patch the following 12 positions of the char table, in order:
 * 0x23  0x24  0x40  0x5b  0x5c  0x5d  0x5e  0x60  0x7b  0x7c  0x7d  0x7e
 * TODO: Implement the missing international charsets
 */
static const uint16_t intCharSets[15][12] = {
    { 0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d,	/* 0 USA */
      0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e	},

    { 0x0023, 0x0024, 0x00e0, 0x00ba, 0x00e7, 0x00a7,	/* 1 France */
      0x005e, 0x0060, 0x00e9, 0x00f9, 0x00e8, 0x00a8	},

    { 0x0023, 0x0024, 0x00a7, 0x00c4, 0x00d6, 0x00dc,	/* 2 Germany */
      0x005e, 0x0060, 0x00e4, 0x00f6, 0x00fc, 0x00df	},

    { 0x00a3, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d,	/* 3 UK */
      0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e	},

    { 0x0023, 0x0024, 0x0040, 0x00c6, 0x00d8, 0x00c5,	/* 4 Denmark (1) */
      0x005e, 0x0060, 0x00e6, 0x00f8, 0x00e5, 0x007e	},

    { 0x0023, 0x00a4, 0x00c9, 0x00c4, 0x00d6, 0x00c5,	/* 5 Sweden */
      0x00dc, 0x00e9, 0x00e4, 0x00f6, 0x00e5, 0x00fc	},

    { 0x0023, 0x0024, 0x0040, 0x00ba, 0x005c, 0x00e9,	/* 6 Italy */
      0x005e, 0x00f9, 0x00e0, 0x00f2, 0x00e8, 0x00ec	},

    { 0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d,	/* 7 Spain 1 */
      0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e	}, /* TODO */

    { 0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d,	/* 8 Japan (English) */
      0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e	}, /* TODO */

    { 0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d,	/* 9 Norway */
      0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e	}, /* TODO */

    { 0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d,	/* 10 Denmark (2) */
      0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e	}, /* TODO */

    { 0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d,	/* 11 Spain (2) */
      0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e	}, /* TODO */

    { 0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d,	/* 12 Latin America */
      0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e	}, /* TODO */

    { 0x0023, 0x0024, 0x0040, 0x005b, 0x005c, 0x005d,	/* 13 Korea */
      0x005e, 0x0060, 0x007b, 0x007c, 0x007d, 0x007e	}, /* TODO */

    { 0x0023, 0x0024, 0x00a7, 0x00c4, 0x0027, 0x0022,	/* 14 Legal */
      0x00b6, 0x0060, 0x00a9, 0x00ae, 0x2020, 0x2122	}
};


#ifdef ENABLE_ESCP_LOG
int escp_do_log = ENABLE_ESCP_LOG;


static void
escp_log(const char *fmt, ...)
{
    va_list ap;

    if (escp_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define escp_log(fmt, ...)
#endif


/* Dump the current page into a formatted file. */
static void
dump_page(escp_t *dev)
{
    char path[1024];

    strcpy(path, dev->pagepath);
    strcat(path, dev->page_fn);
    png_write_rgb(path, dev->page->pixels, dev->page->w, dev->page->h, dev->page->pitch, dev->palcol);
}


static void
new_page(escp_t *dev, int8_t save, int8_t resetx)
{
    /* Dump the current page if needed. */
    if (save && dev->page)
	dump_page(dev);
    if (resetx)
	dev->curr_x = dev->left_margin;

    /* Clear page. */
    dev->curr_y = dev->top_margin;
    if (dev->page) {
	dev->page->dirty = 0;
	memset(dev->page->pixels, 0x00, dev->page->pitch * dev->page->h);
    }

    /* Make the page's file name. */
    plat_tempfile(dev->page_fn, NULL, ".png");
}


static void
pulse_timer(void *priv)
{
    escp_t *dev = (escp_t *) priv;

    if (dev->ack) {
	dev->ack = 0;
	lpt_irq(dev->lpt, 1);
    }

    timer_disable(&dev->pulse_timer);
}


static void
timeout_timer(void *priv)
{
    escp_t *dev = (escp_t *) priv;

    if (dev->page->dirty)
	new_page(dev, 1, 1);

    timer_disable(&dev->timeout_timer);
}


static void
fill_palette(uint8_t redmax, uint8_t greenmax, uint8_t bluemax, uint8_t colorID, escp_t *dev)
{
    uint8_t colormask;
    int i;

    float red = (float)redmax / (float)30.9;
    float green = (float)greenmax / (float)30.9;
    float blue = (float)bluemax / (float)30.9;

    colormask = colorID<<=5;

    for(i = 0; i < 32; i++) {
	dev->palcol[i+colormask].r = 255 - (uint8_t)floor(red * (float)i);
	dev->palcol[i+colormask].g = 255 - (uint8_t)floor(green * (float)i);
	dev->palcol[i+colormask].b = 255 - (uint8_t)floor(blue * (float)i);
    }
}


static void
reset_printer(escp_t *dev)
{
    int i;

    /* TODO: these should be configurable. */
    dev->color = COLOR_BLACK;
    dev->curr_x = dev->curr_y = 0.0;
    dev->esc_seen = 0;
    dev->fss_seen = 0;
    dev->esc_pending = 0;
    dev->esc_parms_req = dev->esc_parms_curr = 0;
    dev->top_margin = dev->left_margin = 0.0;
    dev->right_margin = dev->page_width = PAGE_WIDTH;
    dev->bottom_margin = dev->page_height = PAGE_HEIGHT;
    dev->lpi = PAGE_LPI;
    dev->linespacing = 1.0 / dev->lpi;
    dev->cpi = PAGE_CPI;
    dev->curr_char_table = 1;
    dev->font_style = 0;
    dev->extra_intra_space = 0.0;
    dev->print_upper_control = 1;
    dev->bg_remaining_bytes = 0;
    dev->density_k = 0;
    dev->density_l = 1;
    dev->density_y = 2;
    dev->density_z = 3;
    dev->char_tables[0] = 0; /* italics */
    dev->char_tables[1] = dev->char_tables[2] = dev->char_tables[3] = 437; /* all other tables use CP437 */
    dev->defined_unit = -1.0;
    dev->multipoint_mode = 0;
    dev->multipoint_size = 0.0;
    dev->multipoint_cpi = 0.0;
    dev->hmi = -1;
    dev->msb = 255;
    dev->print_everything_count = 0;
    dev->lq_typeface = TYPEFACE_COURIER;

    init_codepage(dev, dev->char_tables[dev->curr_char_table]);

    update_font(dev);

    new_page(dev, 0, 1);

    for (i = 0; i < 32; i++)
	dev->horizontal_tabs[i] = i * 8.0 * (1.0 / dev->cpi);
    dev->num_horizontal_tabs = 32;
    dev->num_vertical_tabs = -1;

    if (dev->page != NULL)
	dev->page->dirty = 0;

    escp_log("ESC/P: width=%.1fin,height=%.1fin dpi=%i cpi=%i lpi=%i\n",
	     dev->page_width, dev->page_height, (int)dev->dpi,
	     (int)dev->cpi, (int)dev->lpi);
}


static void
reset_printer_hard(escp_t *dev)
{
    dev->ack = 0;
    timer_disable(&dev->pulse_timer);
    timer_disable(&dev->timeout_timer);
    reset_printer(dev);
}


/* Select a ASCII->Unicode mapping by CP number */
static void
init_codepage(escp_t *dev, uint16_t num)
{
    /* Get the codepage map for this number. */
    select_codepage(num, dev->curr_cpmap);
}


static void
update_font(escp_t *dev)
{
    char path[1024];
    char *fn;
    FT_Matrix matrix;
    double hpoints = 10.5;
    double vpoints = 10.5;

    /* We need the FreeType library. */
    if (ft_lib == NULL)
	return;

    /* Release current font if we have one. */
    if (dev->fontface)
	ft_Done_Face(dev->fontface);

    if (dev->print_quality == QUALITY_DRAFT)
	fn = FONT_FILE_DOTMATRIX;
    else switch (dev->lq_typeface) {
	case TYPEFACE_ROMAN:
		fn = FONT_FILE_ROMAN;
		break;
	case TYPEFACE_SANSSERIF:
		fn = FONT_FILE_SANSSERIF;
		break;
	case TYPEFACE_COURIER:
		fn = FONT_FILE_COURIER;
		break;
	case TYPEFACE_SCRIPT:
		fn = FONT_FILE_SCRIPT;
		break;
	case TYPEFACE_OCRA:
		fn = FONT_FILE_OCRA;
		break;
	case TYPEFACE_OCRB:
		fn = FONT_FILE_OCRB;
		break;
	default:
		fn = FONT_FILE_DOTMATRIX;
    }

    /* Create a full pathname for the ROM file. */
    strcpy(path, dev->fontpath);
    path_slash(path);
    strcat(path, fn);

    escp_log("Temp file=%s\n", path);

    /* Load the new font. */
    if (ft_New_Face(ft_lib, path, 0, &dev->fontface)) {
	escp_log("ESC/P: unable to load font '%s'\n", path);
	dev->fontface = NULL;
    }

    if (!dev->multipoint_mode) {
	dev->actual_cpi = dev->cpi;

	if (!(dev->font_style & STYLE_CONDENSED)) {
		hpoints *= 10.0 / dev->cpi;
		vpoints *= 10.0 / dev->cpi;
	}

	if (!(dev->font_style & STYLE_PROP)) {
		if ((dev->cpi == 10.0) && (dev->font_style & STYLE_CONDENSED)) {
			dev->actual_cpi = 17.14;
			hpoints *= 10.0 / 17.14;
		}

		if ((dev->cpi == 12) && (dev->font_style & STYLE_CONDENSED)) {
			dev->actual_cpi = 20.0;
			hpoints *= 10.0 / 20.0;
			vpoints *= 10.0 / 12.0;
		}
	}
	else if (dev->font_style & STYLE_CONDENSED)
		hpoints /= 2.0;

	if ((dev->font_style & STYLE_DOUBLEWIDTH) ||
	    (dev->font_style & STYLE_DOUBLEWIDTHONELINE)) {
		dev->actual_cpi /= 2.0;
		hpoints *= 2.0;
	}

	if (dev->font_style & STYLE_DOUBLEHEIGHT)
		vpoints *= 2.0;
    } else {
	/* Multipoint mode. */
	dev->actual_cpi = dev->multipoint_cpi;
	hpoints = vpoints = dev->multipoint_size;
    }

    if ((dev->font_style & STYLE_SUPERSCRIPT) || (dev->font_style & STYLE_SUBSCRIPT)) {
	hpoints *= 2.0 / 3.0;
	vpoints *= 2.0 / 3.0;
	dev->actual_cpi /= 2.0 / 3.0;
    }

    ft_Set_Char_Size(dev->fontface,
		     (uint16_t)(hpoints * 64), (uint16_t)(vpoints * 64),
		     dev->dpi, dev->dpi);

    if ((dev->font_style & STYLE_ITALICS) ||
	(dev->char_tables[dev->curr_char_table] == 0)) {
	/* Italics transformation. */
	matrix.xx = 0x10000L;
	matrix.xy = (FT_Fixed)(0.20 * 0x10000L);
	matrix.yx = 0;
	matrix.yy = 0x10000L;
	ft_Set_Transform(dev->fontface, &matrix, 0);
    }
}


/* This is the actual ESC/P interpreter. */
static int
process_char(escp_t *dev, uint8_t ch)
{
    double new_x, new_y;
    double move_to;
    double unit_size;
    double reverse;
    double new_top, new_bottom;
    uint16_t rel_move;
    int16_t i;

    escp_log("Esc_seen=%d, fss_seen=%d\n", dev->esc_seen, dev->fss_seen);
    /* Determine number of additional command params that are expected. */
    if (dev->esc_seen || dev->fss_seen) {
	dev->esc_pending = ch;
	if (dev->fss_seen)
		dev->esc_pending |= 0x800;
	dev->esc_seen = dev->fss_seen = 0;
	dev->esc_parms_curr = 0;

	escp_log("Command pending=%02x, font path=%s\n", dev->esc_pending, dev->fontpath);
	switch (dev->esc_pending) {
		case 0x02: // Undocumented
		case 0x0a: // Reverse line feed
		case 0x0c: // Return to top of current page
		case 0x0e: // Select double-width printing (one line) (ESC SO)
		case 0x0f: // Select condensed printing (ESC SI)
		case 0x23: // Cancel MSB control (ESC #)
		case 0x30: // Select 1/8-inch line spacing (ESC 0)
		case 0x31: // Select 7/60-inch line spacing
		case 0x32: // Select 1/6-inch line spacing (ESC 2)
		case 0x34: // Select italic font (ESC 4)
		case 0x35: // Cancel italic font (ESC 5)
		case 0x36: // Enable printing of upper control codes (ESC 6)
		case 0x37: // Enable upper control codes (ESC 7)
		case 0x38: // Disable paper-out detector
		case 0x39: // Enable paper-out detector
		case 0x3c: // Unidirectional mode (one line) (ESC <)
		case 0x3d: // Set MSB to 0 (ESC =)
		case 0x3e: // Set MSB to 1 (ESC >)
		case 0x40: // Initialize printer (ESC @)
		case 0x45: // Select bold font (ESC E)
		case 0x46: // Cancel bold font (ESC F)
		case 0x47: // Select double-strike printing (ESC G)
		case 0x48: // Cancel double-strike printing (ESC H)
		case 0x4d: // Select 10.5-point, 12-cpi (ESC M)
		case 0x4f: // Cancel bottom margin
		case 0x50: // Select 10.5-point, 10-cpi (ESC P)
		case 0x54: // Cancel superscript/subscript printing (ESC T)
		case 0x5e: // Enable printing of all character codes on next character
		case 0x67: // Select 10.5-point, 15-cpi (ESC g)

		case 0x834: // Select italic font (FS 4)	(= ESC 4)
		case 0x835: // Cancel italic font (FS 5)	(= ESC 5)
		case 0x846: // Select forward feed mode (FS F)
		case 0x852: // Select reverse feed mode (FS R)
			dev->esc_parms_req = 0;
			break;

		case 0x19: // Control paper loading/ejecting (ESC EM)
		case 0x20: // Set intercharacter space (ESC SP)
		case 0x21: // Master select (ESC !)
		case 0x2b: // Set n/360-inch line spacing (ESC +)
		case 0x2d: // Turn underline on/off (ESC -)
		case 0x2f: // Select vertical tab channel (ESC /)
		case 0x33: // Set n/180-inch line spacing (ESC 3)
		case 0x41: // Set n/60-inch line spacing
		case 0x43: // Set page length in lines (ESC C)
		case 0x49: // Select character type and print pitch
		case 0x4a: // Advance print position vertically (ESC J n)
		case 0x4e: // Set bottom margin (ESC N)
		case 0x51: // Set right margin (ESC Q)
		case 0x52: // Select an international character set (ESC R)
		case 0x53: // Select superscript/subscript printing (ESC S)
		case 0x55: // Turn unidirectional mode on/off (ESC U)
		case 0x57: // Turn double-width printing on/off (ESC W)
		case 0x61: // Select justification (ESC a)
		case 0x66: // Absolute horizontal tab in columns [conflict]
		case 0x68: // Select double or quadruple size
		case 0x69: // Immediate print
		case 0x6a: // Reverse paper feed
		case 0x6b: // Select typeface (ESC k)
		case 0x6c: // Set left margin (ESC 1)
		case 0x70: // Turn proportional mode on/off (ESC p)
		case 0x72: // Select printing color (ESC r)
		case 0x73: // Select low-speed mode (ESC s)
		case 0x74: // Select character table (ESC t)
		case 0x77: // Turn double-height printing on/off (ESC w)
		case 0x78: // Select LQ or draft (ESC x)
		case 0x7e: // Select/Deselect slash zero (ESC ~)
		case 0x832: // Select 1/6-inch line spacing (FS 2)	(= ESC 2)
		case 0x833: // Set n/360-inch line spacing (FS 3)	(= ESC +)
		case 0x841: // Set n/60-inch line spacing (FS A)	(= ESC A)
		case 0x843: // Select LQ type style (FS C)	(= ESC k)
		case 0x845: // Select character width (FS E)
		case 0x849: // Select character table (FS I)	(= ESC t)
		case 0x853: // Select High Speed/High Density elite pitch (FS S)
		case 0x856: // Turn double-height printing on/off (FS V)	(= ESC w)
			dev->esc_parms_req = 1;
			break;

		case 0x24: // Set absolute horizontal print position (ESC $)
		case 0x3f: // Reassign bit-image mode (ESC ?)
		case 0x4b: // Select 60-dpi graphics (ESC K)
		case 0x4c: // Select 120-dpi graphics (ESC L)
		case 0x59: // Select 120-dpi, double-speed graphics (ESC Y)
		case 0x5a: // Select 240-dpi graphics (ESC Z)
		case 0x5c: // Set relative horizontal print position (ESC \)
		case 0x63: // Set horizontal motion index (HMI) (ESC c)
		case 0x65: // Set vertical tab stops every n lines (ESC e)
		case 0x85a: // Print 24-bit hex-density graphics (FS Z)
			dev->esc_parms_req = 2;
			break;

		case 0x2a: // Select bit image (ESC *)
		case 0x58: // Select font by pitch and point (ESC X)
			dev->esc_parms_req = 3;
			break;

		case 0x5b: // Select character height, width, line spacing
			dev->esc_parms_req = 7;
			break;

		case 0x62: // Set vertical tabs in VFU channels (ESC b)
		case 0x42: // Set vertical tabs (ESC B)
			dev->num_vertical_tabs = 0;
			return 1;

		case 0x44: // Set horizontal tabs (ESC D)
			dev->num_horizontal_tabs = 0;
			return 1;

		case 0x25: // Select user-defined set (ESC %)
		case 0x26: // Define user-defined characters (ESC &)
		case 0x3a: // Copy ROM to RAM (ESC :)
			escp_log("ESC/P: User-defined characters not supported (0x%02x).\n", dev->esc_pending);
			return 1;

		case 0x28: // Two bytes sequence
			/* return and wait for second ESC byte */
			return 1;

		case 0x2e:
			fatal("ESC/P: Print Raster Graphics (2E) command is not implemented.\nTerminating the emulator to avoid endless PNG generation.\n");
			exit(-1);
			return 1;

		default:
			escp_log("ESC/P: Unknown command ESC %c (0x%02x). Unable to skip parameters.\n",
				 dev->esc_pending >= 0x20 ? dev->esc_pending : '?', dev->esc_pending);
			dev->esc_parms_req = 0;
			dev->esc_pending = 0;
			return 1;
	}

	if (dev->esc_parms_req > 0) {
		/* return and wait for parameters to appear */
		return 1;
	}
    }

    /* parameter checking for the 2-byte ESC/P2 commands */
    if (dev->esc_pending == '(') {
	dev->esc_pending = 0x0200 + ch;

	escp_log("Two-byte command pending=%03x, font path=%s\n", dev->esc_pending, dev->fontpath);
	switch (dev->esc_pending) {
		case 0x0242: // Bar code setup and print (ESC (B)
		case 0x025e: // Print data as characters (ESC (^)
			dev->esc_parms_req = 2;
			break;

		case 0x0255: // Set unit (ESC (U)
			dev->esc_parms_req = 3;
			break;

		case 0x0243: // Set page length in defined unit (ESC (C)
		case 0x0256: // Set absolute vertical print position (ESC (V)
		case 0x0276: // Set relative vertical print position (ESC (v)
			dev->esc_parms_req = 4;
			break;

		case 0x0228: // Assign character table (ESC (t)
		case 0x022d: // Select line/score (ESC (-)
			dev->esc_parms_req = 5;
			break;

		case 0x0263: // Set page format (ESC (c)
			dev->esc_parms_req = 6;
			break;

		default:
			/* ESC ( commands are always followed by a "number of parameters" word parameter */
			dev->esc_parms_req = 2;
			dev->esc_pending = 0x101; /* dummy value to be checked later */
			return 1;
	}

	/* If we need parameters, return and wait for them to appear. */
	if (dev->esc_parms_req > 0)
		return 1;
    }

    /* Ignore VFU channel setting. */
    if (dev->esc_pending == 0x62) {
	dev->esc_pending = 0x42;
	return 1;
    }

    /* Collect vertical tabs. */
    if (dev->esc_pending == 0x42) {
	/* check if we're done */
	if ((ch == 0) ||
	    (dev->num_vertical_tabs > 0 && dev->vertical_tabs[dev->num_vertical_tabs - 1] > (double)ch * dev->linespacing)) {
		dev->esc_pending = 0;
	} else {
		if (dev->num_vertical_tabs >= 0 && dev->num_vertical_tabs < 16)
			dev->vertical_tabs[dev->num_vertical_tabs++] = (double)ch * dev->linespacing;
	}
    }

    /* Collect horizontal tabs. */
    if (dev->esc_pending == 0x44) {
	/* check if we're done... */
	if ((ch == 0) ||
	    (dev->num_horizontal_tabs > 0 && dev->horizontal_tabs[dev->num_horizontal_tabs - 1] > (double)ch * (1.0 / dev->cpi))) {
		dev->esc_pending = 0;
	} else {
		if (dev->num_horizontal_tabs < 32)
			dev->horizontal_tabs[dev->num_horizontal_tabs++] = (double)ch * (1.0 / dev->cpi);
	}
    }

    /* Check if we're still collecting parameters for the current command. */
    if (dev->esc_parms_curr < dev->esc_parms_req) {
	/* store current parameter */
	dev->esc_parms[dev->esc_parms_curr++] = ch;

	/* do we still need to continue collecting parameters? */
	if (dev->esc_parms_curr < dev->esc_parms_req)
		return 1;
    }

    /* Handle the pending ESC command. */
    if (dev->esc_pending != 0) {
	switch (dev->esc_pending) {
		case 0x02:	/* undocumented; ignore */
			break;

		case 0x0e:	/* select double-width (one line) (ESC SO) */
			if (! dev->multipoint_mode) {
				dev->hmi = -1;
				dev->font_style |= STYLE_DOUBLEWIDTHONELINE;
				update_font(dev);
			}
			break;

		case 0x0f:	/* select condensed printing (ESC SI) */
			if (! dev->multipoint_mode && (dev->cpi != 15.0)) {
				dev->hmi = -1;
				dev->font_style |= STYLE_CONDENSED;
				update_font(dev);
			}
			break;

		case 0x19:	/* control paper loading/ejecting (ESC EM) */
				/* We are not really loading paper, so most
				 * commands can be ignored */
			if (dev->esc_parms[0] == 'R')
				new_page(dev, 1, 0);

			break;
		case 0x20:	/* set intercharacter space (ESC SP) */
			if (! dev->multipoint_mode) {
				dev->extra_intra_space = (double)dev->esc_parms[0] / (dev->print_quality == QUALITY_DRAFT ? 120.0 : 180.0);
				dev->hmi = -1;
				update_font(dev);
			}
			break;

		case 0x21:	/* master select (ESC !) */
			dev->cpi = dev->esc_parms[0] & 0x01 ? 12.0 : 10.0;

			/* Reset first seven bits. */
			dev->font_style &= 0xFF80;
			if (dev->esc_parms[0] & 0x02)
				dev->font_style |= STYLE_PROP;
			if (dev->esc_parms[0] & 0x04)
				dev->font_style |= STYLE_CONDENSED;
			if (dev->esc_parms[0] & 0x08)
				dev->font_style |= STYLE_BOLD;
			if (dev->esc_parms[0] & 0x10)
				dev->font_style |= STYLE_DOUBLESTRIKE;
			if (dev->esc_parms[0] & 0x20)
				dev->font_style |= STYLE_DOUBLEWIDTH;
			if (dev->esc_parms[0] & 0x40)
				dev->font_style |= STYLE_ITALICS;
			if (dev->esc_parms[0] & 0x80) {
				dev->font_score = SCORE_SINGLE;
				dev->font_style |= STYLE_UNDERLINE;
			}

			dev->hmi = -1;
			dev->multipoint_mode = 0;
			update_font(dev);
			break;

		case 0x23:	/* cancel MSB control (ESC #) */
			dev->msb = 255;
			break;

		case 0x24:	/* set abs horizontal print position (ESC $) */
			unit_size = dev->defined_unit;
			if (unit_size < 0)
				unit_size = 60.0;

			new_x = dev->left_margin + ((double)PARAM16(0) / unit_size);
			if (new_x <= dev->right_margin)
				dev->curr_x = new_x;
			break;

		case 0x85a:	/* Print 24-bit hex-density graphics (FS Z) */
			setup_bit_image(dev, 40, PARAM16(0));
			break;

		case 0x2a:	/* select bit image (ESC *) */
			setup_bit_image(dev, dev->esc_parms[0], PARAM16(1));
			break;

		case 0x2b:	/* set n/360-inch line spacing (ESC +) */
		case 0x833:     /* Set n/360-inch line spacing (FS 3) */
			dev->linespacing = (double)dev->esc_parms[0] / 360.0;
			break;

		case 0x2d:	/* turn underline on/off (ESC -) */
			if (dev->esc_parms[0] == 0 || dev->esc_parms[0] == '0')
				dev->font_style &= ~STYLE_UNDERLINE;
			if (dev->esc_parms[0] == 1 || dev->esc_parms[0] == '1') {
				dev->font_style |= STYLE_UNDERLINE;
				dev->font_score = SCORE_SINGLE;
			}
			update_font(dev);
			break;

		case 0x2f:	/* select vertical tab channel (ESC /) */
			/* Ignore */
			break;

		case 0x30:	/* select 1/8-inch line spacing (ESC 0) */
			dev->linespacing = 1.0 / 8.0;
			break;

		case 0x31:	/* select 7/60-inch line spacing */
			dev->linespacing = 7.0 / 60.0;
			break;

		case 0x32:	/* select 1/6-inch line spacing (ESC 2) */
			dev->linespacing = 1.0 / 6.0;
			break;

		case 0x33:	/* set n/180-inch line spacing (ESC 3) */
			dev->linespacing = (double)dev->esc_parms[0] / 180.0;
			break;

		case 0x34:	/* select italic font (ESC 4) */
			dev->font_style |= STYLE_ITALICS;
			update_font(dev);
			break;

		case 0x35:	/* cancel italic font (ESC 5) */
			dev->font_style &= ~STYLE_ITALICS;
			update_font(dev);
			break;

		case 0x36:	/* enable printing of upper control codes (ESC 6) */
			dev->print_upper_control = 1;
			break;

		case 0x37:	/* enable upper control codes (ESC 7) */
			dev->print_upper_control = 0;
			break;

		case 0x3c:	/* unidirectional mode (one line) (ESC <) */
				/* We don't have a print head, so just
				 * ignore this. */
			break;

		case 0x3d:	/* set MSB to 0 (ESC =) */
			dev->msb = 0;
			break;

		case 0x3e:	/* set MSB to 1 (ESC >) */
			dev->msb = 1;
			break;

		case 0x3f:	/* reassign bit-image mode (ESC ?) */
			if (dev->esc_parms[0] == 'K')
				dev->density_k = dev->esc_parms[1];
			if (dev->esc_parms[0] == 'L')
				dev->density_l = dev->esc_parms[1];
			if (dev->esc_parms[0] == 'Y')
				dev->density_y = dev->esc_parms[1];
			if (dev->esc_parms[0] == 'Z')
				dev->density_z = dev->esc_parms[1];
			break;

		case 0x40:	/* initialize printer (ESC @) */
			reset_printer(dev);
			break;

		case 0x41:	/* set n/60-inch line spacing */
		case 0x841:
			dev->linespacing = (double)dev->esc_parms[0] / 60.0;
			break;

		case 0x43:	/* set page length in lines (ESC C) */
			if (dev->esc_parms[0] != 0) {
				dev->page_height = dev->bottom_margin = (double)dev->esc_parms[0] * dev->linespacing;
			} else {	/* == 0 => Set page length in inches */
				dev->esc_parms_req = 1;
				dev->esc_parms_curr = 0;
				dev->esc_pending = 0x100; /* dummy value for later */
				return 1;
			}
			break;

		case 0x45:	/* select bold font (ESC E) */
			dev->font_style |= STYLE_BOLD;
			update_font(dev);
			break;

		case 0x46:	/* cancel bold font (ESC F) */
			dev->font_style &= ~STYLE_BOLD;
			update_font(dev);
			break;

		case 0x47:	/* select dobule-strike printing (ESC G) */
			dev->font_style |= STYLE_DOUBLESTRIKE;
			break;

		case 0x48:	/* cancel double-strike printing (ESC H) */
			dev->font_style &= ~STYLE_DOUBLESTRIKE;
			break;

		case 0x4a:	/* advance print pos vertically (ESC J n) */
			dev->curr_y += (double)((double)dev->esc_parms[0] / 180.0);
			if (dev->curr_y > dev->bottom_margin)
				new_page(dev, 1, 0);
			break;

		case 0x4b:	/* select 60-dpi graphics (ESC K) */
			/* TODO: graphics stuff */
			setup_bit_image(dev, dev->density_k, PARAM16(0));
			break;

		case 0x4c:	/* select 120-dpi graphics (ESC L) */
			/* TODO: graphics stuff */
			setup_bit_image(dev, dev->density_l, PARAM16(0));
			break;

		case 0x4d:	/* select 10.5-point, 12-cpi (ESC M) */
			dev->cpi = 12.0;
			dev->hmi = -1;
			dev->multipoint_mode = 0;
			update_font(dev);
			break;

		case 0x4e:	/* set bottom margin (ESC N) */
			dev->top_margin = 0.0;
			dev->bottom_margin = (double)dev->esc_parms[0] * dev->linespacing;
			break;

		case 0x4f:	/* cancel bottom (and top) margin */
			dev->top_margin = 0.0;
			dev->bottom_margin = dev->page_height;
			break;

		case 0x50:	/* select 10.5-point, 10-cpi (ESC P) */
			dev->cpi = 10.0;
			dev->hmi = -1;
			dev->multipoint_mode = 0;
			update_font(dev);
			break;

		case 0x51:	/* set right margin */
			dev->right_margin = ((double)dev->esc_parms[0] - 1.0) / dev->cpi;
			break;

		case 0x52:	/* select an intl character set (ESC R) */
			if (dev->esc_parms[0] <= 13 || dev->esc_parms[0] == '@') {
				if (dev->esc_parms[0] == '@')
					dev->esc_parms[0] = 14;

				dev->curr_cpmap[0x23] = intCharSets[dev->esc_parms[0]][0];
				dev->curr_cpmap[0x24] = intCharSets[dev->esc_parms[0]][1];
				dev->curr_cpmap[0x40] = intCharSets[dev->esc_parms[0]][2];
				dev->curr_cpmap[0x5b] = intCharSets[dev->esc_parms[0]][3];
				dev->curr_cpmap[0x5c] = intCharSets[dev->esc_parms[0]][4];
				dev->curr_cpmap[0x5d] = intCharSets[dev->esc_parms[0]][5];
				dev->curr_cpmap[0x5e] = intCharSets[dev->esc_parms[0]][6];
				dev->curr_cpmap[0x60] = intCharSets[dev->esc_parms[0]][7];
				dev->curr_cpmap[0x7b] = intCharSets[dev->esc_parms[0]][8];
				dev->curr_cpmap[0x7c] = intCharSets[dev->esc_parms[0]][9];
				dev->curr_cpmap[0x7d] = intCharSets[dev->esc_parms[0]][10];
				dev->curr_cpmap[0x7e] = intCharSets[dev->esc_parms[0]][11];
			}
			break;

		case 0x53:	/* select superscript/subscript printing (ESC S) */
			if (dev->esc_parms[0] == 0 || dev->esc_parms[0] == '0')
				dev->font_style |= STYLE_SUBSCRIPT;
			if (dev->esc_parms[0] == 1 || dev->esc_parms[1] == '1')
				dev->font_style |= STYLE_SUPERSCRIPT;
			update_font(dev);
			break;

		case 0x54:	/* cancel superscript/subscript printing (ESC T) */
			dev->font_style &= 0xFFFF - STYLE_SUPERSCRIPT - STYLE_SUBSCRIPT;
			update_font(dev);
			break;

		case 0x55:	/* turn unidirectional mode on/off (ESC U) */
			/* We don't have a print head, so just ignore this. */
			break;

		case 0x57:	/* turn double-width printing on/off (ESC W) */
			if (!dev->multipoint_mode) {
				dev->hmi = -1;
				if (dev->esc_parms[0] == 0 || dev->esc_parms[0] == '0')
					dev->font_style &= ~STYLE_DOUBLEWIDTH;
				if (dev->esc_parms[0] == 1 || dev->esc_parms[0] == '1')
					dev->font_style |= STYLE_DOUBLEWIDTH;
				update_font(dev);
			}
			break;

		case 0x58:	/* select font by pitch and point (ESC X) */
			dev->multipoint_mode = 1;
			/* Copy currently non-multipoint CPI if no value was set so far. */
			if (dev->multipoint_cpi == 0.0) {
				dev->multipoint_cpi= dev->cpi;
			}
			if (dev->esc_parms[0] > 0) {	/* set CPI */
				if (dev->esc_parms[0] == 1) {
					/* Proportional spacing. */
					dev->font_style |= STYLE_PROP;
				} else if (dev->esc_parms[0] >= 5)
					dev->multipoint_cpi = 360.0 / (double)dev->esc_parms[0];
			}
			if (dev->multipoint_size == 0.0)
				dev->multipoint_size = 10.5;
			if (PARAM16(1) > 0) {
				/* set points */
				dev->multipoint_size = ((double)PARAM16(1)) / 2.0;
			}
			update_font(dev);
			break;

		case 0x59:	/* select 120-dpi, double-speed graphics (ESC Y) */
			/* TODO: graphics stuff */
			setup_bit_image(dev, dev->density_y, PARAM16(0));
			break;

		case 0x5a:	/* select 240-dpi graphics (ESC Z) */
			/* TODO: graphics stuff */
			setup_bit_image(dev, dev->density_z, PARAM16(0));
			break;

		case 0x5c:	/* set relative horizontal print pos (ESC \) */
			rel_move = PARAM16(0);
			unit_size = dev->defined_unit;
			if (unit_size < 0)
				unit_size = (dev->print_quality == QUALITY_DRAFT ? 120.0 : 180.0);
			dev->curr_x += ((double)rel_move / unit_size);
			break;

		case 0x61:	/* select justification (ESC a) */
			/* Ignore. */
			break;

		case 0x63:	/* set horizontal motion index (HMI) (ESC c) */
			dev->hmi = (double)PARAM16(0) / 360.0;
			dev->extra_intra_space = 0.0;
			break;

		case 0x67:	/* select 10.5-point, 15-cpi (ESC g) */
			dev->cpi = 15;
			dev->hmi = -1;
			dev->multipoint_mode = 0;
			update_font(dev);
			break;

		case 0x846: // Select forward feed mode (FS F) - set reverse not implemented yet
			if (dev->linespacing < 0)
				dev->linespacing *= -1;
			break;

		case 0x6a: // Reverse paper feed (ESC j)
			reverse = (double)PARAM16(0) / (double)216.0;
			reverse = dev->curr_y - reverse;
			if (reverse < dev->left_margin)
				dev->curr_y = dev->left_margin;
			else
				dev->curr_y = reverse;
			break;

		case 0x6b:	/* select typeface (ESC k) */
			if (dev->esc_parms[0] <= 11 || dev->esc_parms[0] == 30 || dev->esc_parms[0] == 31) {
				dev->lq_typeface = dev->esc_parms[0];
			}
			update_font(dev);
			break;

		case 0x6c:	/* set left margin (ESC 1) */
			dev->left_margin = ((double)dev->esc_parms[0] - 1.0) / dev->cpi;
			if (dev->curr_x < dev->left_margin)
				dev->curr_x = dev->left_margin;
			break;

		case 0x70:	/* Turn proportional mode on/off (ESC p) */
			if (dev->esc_parms[0] == 0 || dev->esc_parms[0] == '0')
				dev->font_style &= ~STYLE_PROP;
			if (dev->esc_parms[0] == 1 || dev->esc_parms[0] == '1') {
				dev->font_style |= STYLE_PROP;
				dev->print_quality = QUALITY_LQ;
			}
			dev->multipoint_mode = 0;
			dev->hmi = -1;
			update_font(dev);
			break;

		case 0x72:	/* select printing color (ESC r) */
			if (dev->esc_parms[0] == 0 || dev->esc_parms[0] > 6)
				dev->color = COLOR_BLACK;
			else
				dev->color = dev->esc_parms[0] << 5;
			break;

		case 0x73:	/* select low-speed mode (ESC s) */
			/* Ignore. */
			break;

		case 0x74:	/* select character table (ESC t) */
		case 0x849: 	/* Select character table (FS I) */
			if (dev->esc_parms[0] < 4) {
				dev->curr_char_table = dev->esc_parms[0];
			} else if ((dev->esc_parms[0] >= '0') && (dev->esc_parms[0] <= '3')) {
				dev->curr_char_table = dev->esc_parms[0] - '0';
			}
			init_codepage(dev, dev->char_tables[dev->curr_char_table]);
			update_font(dev);
			break;

		case 0x77:	/* turn double-height printing on/off (ESC w) */
			if (! dev->multipoint_mode) {
				if (dev->esc_parms[0] == 0 || dev->esc_parms[0] == '0')
					dev->font_style &= ~STYLE_DOUBLEHEIGHT;
				if (dev->esc_parms[0] == 1 || dev->esc_parms[0] == '1')
					dev->font_style |= STYLE_DOUBLEHEIGHT;
				update_font(dev);
			}
			break;

		case 0x78:	/* select LQ or draft (ESC x) */
			if (dev->esc_parms[0] == 0 || dev->esc_parms[0] == '0') {
				dev->print_quality = QUALITY_DRAFT;
				dev->font_style |= STYLE_CONDENSED;
			}
			if (dev->esc_parms[0] == 1 || dev->esc_parms[0] == '1') {
				dev->print_quality = QUALITY_LQ;
				dev->font_style &= ~STYLE_CONDENSED;
			}
			dev->hmi = -1;
			update_font(dev);
			break;

		/* Our special command markers. */
		case 0x0100:	/* set page length in inches (ESC C NUL) */
			dev->page_height = (double)dev->esc_parms[0];
			dev->bottom_margin = dev->page_height;
			dev->top_margin = 0.0;
			break;

		case 0x0101:	/* skip unsupported ESC ( command */
			dev->esc_parms_req = PARAM16(0);
			dev->esc_parms_curr = 0;
			break;

		/* Extended ESC ( <x> commands */
		case 0x0228:	/* assign character table (ESC (t) */
		case 0x0274:
			if (dev->esc_parms[2] < 4 && dev->esc_parms[3] < 16) {
				dev->char_tables[dev->esc_parms[2]] = codepages[dev->esc_parms[3]];
				if (dev->esc_parms[2] == dev->curr_char_table)
					init_codepage(dev, dev->char_tables[dev->curr_char_table]);
			}
			break;

		case 0x022d:	/* select line/score (ESC (-)  */
			dev->font_style &= ~(STYLE_UNDERLINE | STYLE_STRIKETHROUGH | STYLE_OVERSCORE);
			dev->font_score = dev->esc_parms[4];
			if (dev->font_score) {
				if (dev->esc_parms[3] == 1)
					dev->font_style |= STYLE_UNDERLINE;
				if (dev->esc_parms[3] == 2)
					dev->font_style |= STYLE_STRIKETHROUGH;
				if (dev->esc_parms[3] == 3)
					dev->font_style |= STYLE_OVERSCORE;
			}
			update_font(dev);
			break;

		case 0x0242:	/* bar code setup and print (ESC (B) */
			//ERRLOG("ESC/P: Barcode printing not supported.\n");

			/* Find out how many bytes to skip. */
			dev->esc_parms_req = PARAM16(0);
			dev->esc_parms_curr = 0;
			break;

		case 0x0243:	/* set page length in defined unit (ESC (C) */
			if (dev->esc_parms[0] && (dev->defined_unit> 0)) {
				dev->page_height = dev->bottom_margin = (double)PARAM16(2) * dev->defined_unit;
				dev->top_margin = 0.0;
			}
			break;

		case 0x0255:	/* set unit (ESC (U) */
			dev->defined_unit = 3600.0 / (double)dev->esc_parms[2];
			break;

		case 0x0256:	/* set abse vertical print pos (ESC (V) */
			unit_size = dev->defined_unit;
			if (unit_size < 0)
				unit_size = 360.0;
			new_y = dev->top_margin + (double)PARAM16(2) * unit_size;
			if (new_y > dev->bottom_margin)
				new_page(dev, 1, 0);
			else
				dev->curr_y = new_y;
			break;

		case 0x025e:	/* print data as characters (ESC (^) */
			dev->print_everything_count = PARAM16(0);
			break;

		case 0x0263:	/* set page format (ESC (c) */
			if (dev->defined_unit > 0.0) {
				new_top = (double)PARAM16(2) * dev->defined_unit;
				new_bottom = (double)PARAM16(4) * dev->defined_unit;
				if (new_top >= new_bottom)
					break;
				if (new_top < dev->page_height)
					dev->top_margin = new_top;
				if (new_bottom < dev->page_height)
					dev->bottom_margin = new_bottom;
				if (dev->top_margin > dev->curr_y)
					dev->curr_y = dev->top_margin;
			}
			break;

		case 0x0276:	/* set relative vertical print pos (ESC (v) */
			{
				unit_size = dev->defined_unit;
				if (unit_size < 0.0)
					unit_size = 360.0;
				new_y = dev->curr_y + (double)((int16_t)PARAM16(2)) * unit_size;
				if (new_y > dev->top_margin) {
					if (new_y > dev->bottom_margin)
						new_page(dev, 1, 0);
					else
						dev->curr_y = new_y;
				}
			}
			break;

		default:
			break;
	}

	dev->esc_pending = 0;
	return 1;
    }

    escp_log("CH=%02x\n", ch);

    /* Now handle the "regular" control characters. */
    switch (ch) {
	case 0x00:
		return 1;

	case 0x07:  /* Beeper (BEL) */
		/* TODO: beep? */
		return 1;

	case 0x08:	/* Backspace (BS) */
		new_x = dev->curr_x - (1.0 / dev->actual_cpi);
		if (dev->hmi > 0)
			new_x = dev->curr_x - dev->hmi;
		if (new_x >= dev->left_margin)
			dev->curr_x = new_x;
		return 1;

	case 0x09:	/* Tab horizontally (HT) */
		/* Find tab right to current pos. */
		move_to = -1.0;
		for (i = 0; i < dev->num_horizontal_tabs; i++) {
			if (dev->horizontal_tabs[i] > dev->curr_x)
				move_to = dev->horizontal_tabs[i];
		}

		/* Nothing found or out of page bounds => Ignore. */
		if (move_to > 0.0 && move_to < dev->right_margin)
			dev->curr_x = move_to;

		return 1;

	case 0x0b:	/* Tab vertically (VT) */
		if (dev->num_vertical_tabs == 0) {
			/* All tabs cleared? => Act like CR */
			dev->curr_x = dev->left_margin;
		} else if (dev->num_vertical_tabs < 0) {
			/* No tabs set since reset => Act like LF */
			dev->curr_x = dev->left_margin;
			dev->curr_y += dev->linespacing;
			if (dev->curr_y > dev->bottom_margin)
				new_page(dev, 1, 0);
		} else {
			/* Find tab below current pos. */
			move_to = -1;
			for (i = 0; i < dev->num_vertical_tabs; i++) {
				if (dev->vertical_tabs[i] > dev->curr_y)
					move_to = dev->vertical_tabs[i];
			}

			/* Nothing found => Act like FF. */
			if (move_to > dev->bottom_margin || move_to < 0)
				new_page(dev, 1, 0);
			else
				dev->curr_y = move_to;
		}

		if (dev->font_style & STYLE_DOUBLEWIDTHONELINE) {
			dev->font_style &= 0xFFFF - STYLE_DOUBLEWIDTHONELINE;
			update_font(dev);
		}
		return 1;

	case 0x0c:	/* Form feed (FF) */
		if (dev->font_style & STYLE_DOUBLEWIDTHONELINE) {
			dev->font_style &= ~STYLE_DOUBLEWIDTHONELINE;
			update_font(dev);
		}
		new_page(dev, 1, 1);
		return 1;

	case 0x0d:	/* Carriage Return (CR) */
		dev->curr_x = dev->left_margin;
		if (!dev->autofeed)
			return 1;
		/*FALLTHROUGH*/

	case 0x0a:	/* Line feed */
		if (dev->font_style & STYLE_DOUBLEWIDTHONELINE) {
			dev->font_style &= ~STYLE_DOUBLEWIDTHONELINE;
			update_font(dev);
		}
		dev->curr_x = dev->left_margin;
		dev->curr_y += dev->linespacing;
		if (dev->curr_y > dev->bottom_margin)
			new_page(dev, 1, 0);
		return 1;

	case 0x0e:	/* select Real64-width printing (one line) (SO) */
		if (! dev->multipoint_mode) {
			dev->hmi = -1;
			dev->font_style |= STYLE_DOUBLEWIDTHONELINE;
			update_font(dev);
		}
		return 1;

	case 0x0f:	/* select condensed printing (SI) */
		if (! dev->multipoint_mode) {
			dev->hmi = -1;
			dev->font_style |= STYLE_CONDENSED;
			update_font(dev);
		}
		return 1;

	case 0x11:	/* select printer (DC1) */
		/* Ignore. */
		return 0;

	case 0x12:	/* cancel condensed printing (DC2) */
		dev->hmi = -1;
		dev->font_style &= ~STYLE_CONDENSED;
		update_font(dev);
		return 1;

	case 0x13:	/* deselect printer (DC3) */
		/* Ignore. */
		return 1;

	case 0x14:	/* cancel double-width printing (one line) (DC4) */
		dev->hmi = -1;
		dev->font_style &= ~STYLE_DOUBLEWIDTHONELINE;
		update_font(dev);
		return 1;

	case 0x18:	/* cancel line (CAN) */
		return 1;

	case 0x1b:	/* ESC */
		dev->esc_seen = 1;
		return 1;

	case 0x1c:	/* FS (IBM commands) */
		dev->fss_seen = 1;
		return 1;

	default:
		return 0;
    }

    /* This is a printable character -> print it. */
    return 0;
}


static void
handle_char(escp_t *dev, uint8_t ch)
{
    FT_UInt char_index;
    uint16_t pen_x, pen_y;
    uint16_t line_start, line_y;
    double x_advance;

    if (dev->page == NULL)
	return;

    /* MSB mode */
    if (dev->msb != 255) {
        if (dev->msb == 0)
            ch &= 0x7f;
        else if (dev->msb == 1)
            ch |= 0x80;
    }

    if (dev->bg_remaining_bytes > 0) {
	print_bit_graph(dev, ch);
	return;
    }

    /* "print everything" mode? aka. ESC ( ^ */
    if (dev->print_everything_count > 0) {
	escp_log("Print everything count=%d\n", dev->print_everything_count);
	/* do not process command char, just continue */
	dev->print_everything_count--;
    } else if (process_char(dev, ch)) {
	/* command was processed */
	return;
    }

    /* We cannot print if we have no font loaded. */
    if (dev->fontface == NULL)
	return;

    if (ch == 0x01)
	ch = 0x20;

    /* ok, so we need to print the character now */
    if (ft_lib) {
	char_index = ft_Get_Char_Index(dev->fontface, dev->curr_cpmap[ch]);
	ft_Load_Glyph(dev->fontface, char_index, FT_LOAD_DEFAULT);
	ft_Render_Glyph(dev->fontface->glyph, FT_RENDER_MODE_NORMAL);
    }

    pen_x = PIXX + dev->fontface->glyph->bitmap_left;
    pen_y = (uint16_t)(PIXY - dev->fontface->glyph->bitmap_top + dev->fontface->size->metrics.ascender / 64);

    if (dev->font_style & STYLE_SUBSCRIPT)
	pen_y += dev->fontface->glyph->bitmap.rows / 2;

    /* mark the page as dirty if anything is drawn */
    if ((ch != 0x20) || (dev->font_score != SCORE_NONE))
	dev->page->dirty = 1;

    /* draw the glyph */
    blit_glyph(dev, pen_x, pen_y, 0);
    blit_glyph(dev, pen_x + 1, pen_y, 1);

    /* doublestrike -> draw glyph a second time, 1px below */
    if (dev->font_style & STYLE_DOUBLESTRIKE) {
	blit_glyph(dev, pen_x, pen_y + 1, 1);
	blit_glyph(dev, pen_x + 1, pen_y + 1, 1);
    }

    /* bold -> draw glyph a second time, 1px to the right */
    if (dev->font_style & STYLE_BOLD) {
	blit_glyph(dev, pen_x + 1, pen_y, 1);
	blit_glyph(dev, pen_x + 2, pen_y, 1);
	blit_glyph(dev, pen_x + 3, pen_y, 1);
    }

    line_start = PIXX;

    if (dev->font_style & STYLE_PROP)
	x_advance = dev->fontface->glyph->advance.x / (dev->dpi * 64.0);
    else {
	if (dev->hmi < 0)
		x_advance = 1.0 / dev->actual_cpi;
	else
		x_advance = dev->hmi;
    }

    x_advance += dev->extra_intra_space;
    dev->curr_x += x_advance;

    /* Line printing (underline etc.) */
    if (dev->font_score != SCORE_NONE && (dev->font_style & (STYLE_UNDERLINE | STYLE_STRIKETHROUGH | STYLE_OVERSCORE))) {
	/* Find out where to put the line. */
	line_y = PIXY;

	if (dev->font_style & STYLE_UNDERLINE)
		line_y = (PIXY + (uint16_t)(dev->fontface->size->metrics.height * 0.9));
	if (dev->font_style & STYLE_STRIKETHROUGH)
		line_y = (PIXY + (uint16_t)(dev->fontface->size->metrics.height * 0.45));
	if (dev->font_style & STYLE_OVERSCORE)
		line_y = PIXY - ((dev->font_score == SCORE_DOUBLE || dev->font_score == SCORE_DOUBLEBROKEN) ? 5 : 0);

	draw_hline(dev, pen_x, PIXX, line_y, dev->font_score == SCORE_SINGLEBROKEN || dev->font_score == SCORE_DOUBLEBROKEN);

	if (dev->font_score == SCORE_DOUBLE || dev->font_score == SCORE_DOUBLEBROKEN)
		draw_hline(dev, line_start, PIXX, line_y + 5, dev->font_score == SCORE_SINGLEBROKEN || dev->font_score == SCORE_DOUBLEBROKEN);
    }

    if ((dev->curr_x + x_advance) > dev->right_margin) {
	dev->curr_x = dev->left_margin;
	dev->curr_y += dev->linespacing;
	if (dev->curr_y > dev->bottom_margin)
		new_page(dev, 1, 0);
    }
}


/* TODO: This can be optimized quite a bit... I'm just too lazy right now ;-) */
static void
blit_glyph(escp_t *dev, unsigned destx, unsigned desty, int8_t add)
{
    FT_Bitmap *bitmap = &dev->fontface->glyph->bitmap;
    unsigned x, y;
    uint8_t src, *dst;

    /* check if freetype is available */
    if (ft_lib == NULL)
	return;

    for (y = 0; y < bitmap->rows; y++) {
	for (x = 0; x < bitmap->width; x++) {
		src = *(bitmap->buffer + x + y * bitmap->pitch);
		/* ignore background, and respect page size */
		if (src > 0 && (destx + x < (unsigned)dev->page->w) && (desty + y < (unsigned)dev->page->h)) {
			dst = (uint8_t *)dev->page->pixels + (x + destx) + (y + desty) * dev->page->pitch;
			src >>= 3;

			if (add) {
				if (((*dst) & 0x1f) + src > 31)
					*dst |= (dev->color | 0x1f);
				else {
					*dst += src;
					*dst |= dev->color;
				}
			} else
				*dst = src|dev->color;
		}
	}
    }
}


/* Draw anti-aliased line. */
static void
draw_hline(escp_t *dev, unsigned from_x, unsigned to_x, unsigned y, int8_t broken)
{
    unsigned breakmod = dev->dpi / 15;
    unsigned gapstart = (breakmod * 4) / 5;
    unsigned x;

    for (x = from_x; x <= to_x; x++) {
	/* Skip parts if broken line or going over the border. */
	if ((!broken || (x % breakmod <= gapstart)) && (x < dev->page->w)) {
		if (y > 0 && (y - 1) < dev->page->h)
			*((uint8_t*)dev->page->pixels + x + (y - 1) * (unsigned)dev->page->pitch) = 240;
		if (y < dev->page->h)
			*((uint8_t*)dev->page->pixels + x + y * (unsigned)dev->page->pitch) = !broken ? 255 : 240;
		if (y + 1 < dev->page->h)
			*((uint8_t*)dev->page->pixels + x + (y + 1) * (unsigned)dev->page->pitch) = 240;
	}
    }
}


static void
setup_bit_image(escp_t *dev, uint8_t density, uint16_t num_columns)
{
    escp_log("Density=%d\n", density);
    switch (density) {
	case 0:
		dev->bg_h_density = 60;
		dev->bg_v_density = 60;
		dev->bg_adjacent = 1;
		dev->bg_bytes_per_column = 1;
		break;

	case 1:
		dev->bg_h_density = 120;
		dev->bg_v_density = 60;
		dev->bg_adjacent = 1;
		dev->bg_bytes_per_column = 1;
		break;

	case 2:
		dev->bg_h_density = 120;
		dev->bg_v_density = 60;
		dev->bg_adjacent = 0;
		dev->bg_bytes_per_column = 1;
		break;

	case 3:
		dev->bg_h_density = 60;
		dev->bg_v_density = 240;
		dev->bg_adjacent = 0;
		dev->bg_bytes_per_column = 1;
		break;

	case 4:
		dev->bg_h_density = 80;
		dev->bg_v_density = 60;
		dev->bg_adjacent = 1;
		dev->bg_bytes_per_column = 1;
		break;

	case 6:
		dev->bg_h_density = 90;
		dev->bg_v_density = 60;
		dev->bg_adjacent = 1;
		dev->bg_bytes_per_column = 1;
		break;

	case 32:
		dev->bg_h_density = 60;
		dev->bg_v_density = 180;
		dev->bg_adjacent = 1;
		dev->bg_bytes_per_column = 3;
		break;

	case 33:
		dev->bg_h_density = 120;
		dev->bg_v_density = 180;
		dev->bg_adjacent = 1;
		dev->bg_bytes_per_column = 3;
		break;

	case 38:
		dev->bg_h_density = 90;
		dev->bg_v_density = 180;
		dev->bg_adjacent = 1;
		dev->bg_bytes_per_column = 3;
		break;

	case 39:
		dev->bg_h_density = 180;
		dev->bg_v_density = 180;
		dev->bg_adjacent = 1;
		dev->bg_bytes_per_column = 3;
		break;

	case 40:
		dev->bg_h_density = 360;
		dev->bg_v_density = 180;
		dev->bg_adjacent = 0;
		dev->bg_bytes_per_column = 3;
		break;

	case 71:
		dev->bg_h_density = 180;
		dev->bg_v_density = 360;
		dev->bg_adjacent = 1;
		dev->bg_bytes_per_column = 6;
		break;

	case 72:
		dev->bg_h_density = 360;
		dev->bg_v_density = 360;
		dev->bg_adjacent = 0;
		dev->bg_bytes_per_column = 6;
		break;

	case 73:
		dev->bg_h_density = 360;
		dev->bg_v_density = 360;
		dev->bg_adjacent = 1;
		dev->bg_bytes_per_column = 6;
		break;

	default:
		escp_log("ESC/P: Unsupported bit image density %d.\n", density);
		break;
    }

    dev->bg_remaining_bytes = num_columns * dev->bg_bytes_per_column;
    dev->bg_bytes_read = 0;
}


static void
print_bit_graph(escp_t *dev, uint8_t ch)
{
    uint8_t pixel_w; /* width of the "pixel" */
    uint8_t pixel_h; /* height of the "pixel" */
    unsigned i, j, xx, yy;
    double old_y;

    dev->bg_column[dev->bg_bytes_read++] = ch;
    dev->bg_remaining_bytes--;

    /* Only print after reading a full column. */
    if (dev->bg_bytes_read < dev->bg_bytes_per_column)
	return;

    old_y = dev->curr_y;

    pixel_w = 1;
    pixel_h = 1;

    if (dev->bg_adjacent) {
	/* if page DPI is bigger than bitgraphics DPI, drawn pixels get "bigger" */
	pixel_w = dev->dpi / dev->bg_h_density > 0 ? dev->dpi / dev->bg_h_density : 1;
	pixel_h = dev->dpi / dev->bg_v_density > 0 ? dev->dpi / dev->bg_v_density : 1;
    }

    for (i = 0; i < dev->bg_bytes_per_column; i++) {
	/* for each byte */
	for (j = 128; j != 0; j >>= 1) {
		/* for each bit */
		if (dev->bg_column[i] & j) {
			/* draw a "pixel" */
			for (xx = 0; xx < pixel_w; xx++) {
				for (yy = 0; yy < pixel_h; yy++) {
					if (((PIXX + xx) < (unsigned)dev->page->w) && ((PIXY + yy) < (unsigned)dev->page->h))
						*((uint8_t *)dev->page->pixels + (PIXX + xx) + (PIXY + yy)*dev->page->pitch) |= (dev->color | 0x1f);
				}
			}
		}

		dev->curr_y += 1.0 / (double)dev->bg_v_density;
	}
    }

    /* Mark page dirty. */
    dev->page->dirty = 1;

    /* Restore Y-position. */
    dev->curr_y = old_y;

    dev->bg_bytes_read = 0;

    /* Advance print head. */
    dev->curr_x += 1.0 / dev->bg_h_density;
}


static void
write_data(uint8_t val, void *priv)
{
    escp_t *dev = (escp_t *)priv;

    if (dev == NULL)
	return;

    dev->data = val;
}


static void
write_ctrl(uint8_t val, void *priv)
{
    escp_t *dev = (escp_t *)priv;

    if (dev == NULL)
	return;

    if (val & 0x08) {		/* SELECT */
	/* select printer */
	dev->select = 1;
    }

    if ((val & 0x04) && !(dev->ctrl & 0x04)) {
	/* reset printer */
	dev->select = 0;

	reset_printer_hard(dev);
    }

    /* Data is strobed to the parallel printer on the falling edge of the
       strobe bit. */
    if (!(val & 0x01) && (dev->ctrl & 0x01)) {
	/* Process incoming character. */
	handle_char(dev, dev->data);

	/* ACK it, will be read on next READ STATUS. */
	dev->ack = 1;
	timer_set_delay_u64(&dev->pulse_timer, ISACONST);

	timer_set_delay_u64(&dev->timeout_timer, 5000000 * TIMER_USEC);
    }

    dev->ctrl = val;

    dev->autofeed = ((val & 0x02) > 0);
}


static uint8_t
read_data(void *priv)
{
    escp_t *dev = (escp_t *)priv;

    return dev->data;
}


static uint8_t
read_ctrl(void *priv)
{
    escp_t *dev = (escp_t *)priv;

    return 0xe0 | (dev->autofeed ? 0x02 : 0x00) | (dev->ctrl & 0xfd);
}


static uint8_t
read_status(void *priv)
{
    escp_t *dev = (escp_t *)priv;
    uint8_t ret = 0x1f;

    ret |= 0x80;

    if (!dev->ack)
	ret |= 0x40;

    return(ret);
}


static void *
escp_init(void *lpt)
{
    const char *fn = PATH_FREETYPE_DLL;
    escp_t *dev;
    int i;

    /* Dynamically load FreeType. */
    if (ft_handle == NULL) {
	ft_handle = dynld_module(fn, ft_imports);
	if (ft_handle == NULL) {
		ui_msgbox_header(MBX_ERROR, (wchar_t *) IDS_2110, (wchar_t *) IDS_2131);
		return(NULL);
	}
    }

    /* Initialize FreeType. */
    if (ft_lib == NULL) {
	if (ft_Init_FreeType(&ft_lib)) {
		ui_msgbox_header(MBX_ERROR, (wchar_t *) IDS_2110, (wchar_t *) IDS_2131);
		dynld_close(ft_lib);
		ft_lib = NULL;
		return(NULL);
	}
    }

    /* Initialize a device instance. */
    dev = (escp_t *)malloc(sizeof(escp_t));
    memset(dev, 0x00, sizeof(escp_t));
    dev->ctrl = 0x04;
    dev->lpt = lpt;

    /* Create a full pathname for the font files. */
    if(strlen(exe_path) >= sizeof(dev->fontpath)) {
	free(dev);
	return(NULL);
    }

    strcpy(dev->fontpath, exe_path);
    path_slash(dev->fontpath);
    strcat(dev->fontpath, "roms/printer/fonts/");

    /* Create the full path for the page images. */
    path_append_filename(dev->pagepath, usr_path, "printer");
    if (! plat_dir_check(dev->pagepath))
        plat_dir_create(dev->pagepath);
    path_slash(dev->pagepath);

    dev->page_width = PAGE_WIDTH;
    dev->page_height = PAGE_HEIGHT;
    dev->dpi = PAGE_DPI;

    /* Create 8-bit grayscale buffer for the page. */
    dev->page = (psurface_t *)malloc(sizeof(psurface_t));
    dev->page->w = (int)(dev->dpi * dev->page_width);
    dev->page->h = (int)(dev->dpi * dev->page_height);
    dev->page->pitch = dev->page->w;
    dev->page->pixels = (uint8_t *)malloc(dev->page->pitch * dev->page->h);
    memset(dev->page->pixels, 0x00, dev->page->pitch * dev->page->h);

    /* Initialize parameters. */
    for (i = 0; i < 32; i++) {
	dev->palcol[i].r = 255;
	dev->palcol[i].g = 255;
	dev->palcol[i].b = 255;
    }

    /* 0 = all white needed for logic 000 */
    fill_palette(  0,   0,   0, 1, dev);
    /* 1 = magenta* 001 */
    fill_palette(  0, 255,   0, 1, dev);
    /* 2 = cyan*    010 */
    fill_palette(255,   0,   0, 2, dev);
    /* 3 = "violet" 011 */
    fill_palette(255, 255,   0, 3, dev);
    /* 4 = yellow*  100 */
    fill_palette(  0,   0, 255, 4, dev);
    /* 5 = red      101 */
    fill_palette(  0, 255, 255, 5, dev);
    /* 6 = green    110 */
    fill_palette(255,   0, 255, 6, dev);
    /* 7 = black    111 */
    fill_palette(255, 255, 255, 7, dev);

    dev->color = COLOR_BLACK;
    dev->fontface = 0;
    dev->autofeed = 0;

    reset_printer(dev);

    escp_log("ESC/P: created a virtual page of dimensions %d x %d pixels.\n",
	     dev->page->w, dev->page->h);

    timer_add(&dev->pulse_timer, pulse_timer, dev, 0);
    timer_add(&dev->timeout_timer, timeout_timer, dev, 0);

    return(dev);
}


static void
escp_close(void *priv)
{
    escp_t *dev = (escp_t *)priv;

    if (dev == NULL) return;

    if (dev->page != NULL) {
	/* Print last page if it contains data. */
	if (dev->page->dirty)
		dump_page(dev);

	if (dev->page->pixels != NULL)
		free(dev->page->pixels);
	free(dev->page);
    }

    free(dev);
}


const lpt_device_t lpt_prt_escp_device = {
    .name = "Generic ESC/P Dot-Matrix",
    .internal_name = "dot_matrix",
    .init = escp_init,
    .close = escp_close,
    .write_data = write_data,
    .write_ctrl = write_ctrl,
    .read_data = read_data,
    .read_status = read_status,
    .read_ctrl = read_ctrl
};
