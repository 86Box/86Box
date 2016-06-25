/*	Code borrowed from DOSBox and adapted by OBattler.
	Original author: reenigne. */

#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "timer.h"
#include "video.h"
#include "vid_cga.h"
#include "vid_cga_comp.h"

int CGA_Composite_Table[1024];

static double brightness = 0;
static double contrast = 100;
static double saturation = 100;
static double sharpness = 0;
static double hue_offset = 0;

// New algorithm by reenigne
// Works in all CGA modes/color settings and can simulate older and newer CGA revisions

static const double tau = 6.28318531; // == 2*pi

static unsigned char chroma_multiplexer[256] = {
	  2,  2,  2,  2, 114,174,  4,  3,   2,  1,133,135,   2,113,150,  4,
	133,  2,  1, 99, 151,152,  2,  1,   3,  2, 96,136, 151,152,151,152,
	  2, 56, 62,  4, 111,250,118,  4,   0, 51,207,137,   1,171,209,  5,
	140, 50, 54,100, 133,202, 57,  4,   2, 50,153,149, 128,198,198,135,
	 32,  1, 36, 81, 147,158,  1, 42,  33,  1,210,254,  34,109,169, 77,
	177,  2,  0,165, 189,154,  3, 44,  33,  0, 91,197, 178,142,144,192,
	  4,  2, 61, 67, 117,151,112, 83,   4,  0,249,255,   3,107,249,117,
	147,  1, 50,162, 143,141, 52, 54,   3,  0,145,206, 124,123,192,193,
	 72, 78,  2,  0, 159,208,  4,  0,  53, 58,164,159,  37,159,171,  1,
	248,117,  4, 98, 212,218,  5,  2,  54, 59, 93,121, 176,181,134,130,
	  1, 61, 31,  0, 160,255, 34,  1,   1, 58,197,166,   0,177,194,  2,
	162,111, 34, 96, 205,253, 32,  1,   1, 57,123,125, 119,188,150,112,
	 78,  4,  0, 75, 166,180, 20, 38,  78,  1,143,246,  42,113,156, 37,
	252,  4,  1,188, 175,129,  1, 37, 118,  4, 88,249, 202,150,145,200,
	 61, 59, 60, 60, 228,252,117, 77,  60, 58,248,251,  81,212,254,107,
	198, 59, 58,169, 250,251, 81, 80, 100, 58,154,250, 251,252,252,252};

static double intensity[4] = {
	77.175381, 88.654656, 166.564623, 174.228438};

#define NEW_CGA(c,i,r,g,b) (((c)/0.72)*0.29 + ((i)/0.28)*0.32 + ((r)/0.28)*0.1 + ((g)/0.28)*0.22 + ((b)/0.28)*0.07)

double mode_brightness;
double mode_contrast;
double mode_hue;
double min_v;
double max_v;

double video_ri, video_rq, video_gi, video_gq, video_bi, video_bq;
int video_sharpness;
int tandy_mode_control = 0;

static bool new_cga = 0;
static bool is_bw = 0;
static bool is_bpp1 = 0;

static uint8_t comp_pal[256][3];

static Bit8u byte_clamp_other(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

FILE *df;

void update_cga16_color(cga_t *cga) {
	int x;
	Bit32u x2;

        if (!new_cga) {
                min_v = chroma_multiplexer[0] + intensity[0];
                max_v = chroma_multiplexer[255] + intensity[3];
        }
        else {
                double i0 = intensity[0];
                double i3 = intensity[3];
                min_v = NEW_CGA(chroma_multiplexer[0], i0, i0, i0, i0);
                max_v = NEW_CGA(chroma_multiplexer[255], i3, i3, i3, i3);
        }
        mode_contrast = 256/(max_v - min_v);
        mode_brightness = -min_v*mode_contrast;
        if ((cga->cgamode & 3) == 1)
                mode_hue = 14;
        else
                mode_hue = 4;

        mode_contrast *= contrast * (new_cga ? 1.2 : 1)/100;             // new CGA: 120%
        mode_brightness += (new_cga ? brightness-10 : brightness)*5;     // new CGA: -10
        double mode_saturation = (new_cga ? 4.35 : 2.9)*saturation/100;  // new CGA: 150%

        for (x = 0; x < 1024; ++x) {
                int phase = x & 3;
                int right = (x >> 2) & 15;
                int left = (x >> 6) & 15;
                int rc = right;
                int lc = left;
                if ((cga->cgamode & 4) != 0) {
                        rc = (right & 8) | ((right & 7) != 0 ? 7 : 0);
                        lc = (left & 8) | ((left & 7) != 0 ? 7 : 0);
                }
                double c =
                        chroma_multiplexer[((lc & 7) << 5) | ((rc & 7) << 2) | phase];
                double i = intensity[(left >> 3) | ((right >> 2) & 2)];
                double v;
                if (!new_cga)
                        v = c + i;
                else {
                        double r = intensity[((left >> 2) & 1) | ((right >> 1) & 2)];
                        double g = intensity[((left >> 1) & 1) | (right & 2)];
                        double b = intensity[(left & 1) | ((right << 1) & 2)];
                        v = NEW_CGA(c, i, r, g, b);
                }
                CGA_Composite_Table[x] = (int) (v*mode_contrast + mode_brightness);
        }

        double i = CGA_Composite_Table[6*68] - CGA_Composite_Table[6*68 + 2];
        double q = CGA_Composite_Table[6*68 + 1] - CGA_Composite_Table[6*68 + 3];

        double a = tau*(33 + 90 + hue_offset + mode_hue)/360.0;
        double c = cos(a);
        double s = sin(a);
        double r = 256*mode_saturation/sqrt(i*i+q*q);

        double iq_adjust_i = -(i*c + q*s)*r;
        double iq_adjust_q = (q*c - i*s)*r;

        static const double ri = 0.9563;
        static const double rq = 0.6210;
        static const double gi = -0.2721;
        static const double gq = -0.6474;
        static const double bi = -1.1069;
        static const double bq = 1.7046;

        video_ri = (int) (ri*iq_adjust_i + rq*iq_adjust_q);
        video_rq = (int) (-ri*iq_adjust_q + rq*iq_adjust_i);
        video_gi = (int) (gi*iq_adjust_i + gq*iq_adjust_q);
        video_gq = (int) (-gi*iq_adjust_q + gq*iq_adjust_i);
        video_bi = (int) (bi*iq_adjust_i + bq*iq_adjust_q);
        video_bq = (int) (-bi*iq_adjust_q + bq*iq_adjust_i);
        video_sharpness = (int) (sharpness*256/100);

#if 0
	df = fopen("CGA_Composite_Table.dmp", "wb");
	fwrite(CGA_Composite_Table, 1024, sizeof(int), df);
	fclose(df);
#endif
}

#if 0
void configure_comp(double h, uint8_t n, uint8_t bw, uint8_t b1)
{
	hue_offset = h;
	new_cga = n;
	is_bw = bw;
	is_bpp1 = b1;
}
#endif

static Bit8u byte_clamp(int v) {
        v >>= 13;
        return v < 0 ? 0 : (v > 255 ? 255 : v);
}

/* 2048x1536 is the maximum we can possibly support. */
#define SCALER_MAXWIDTH 2048

static int temp[SCALER_MAXWIDTH + 10]={0};
static int atemp[SCALER_MAXWIDTH + 2]={0};
static int btemp[SCALER_MAXWIDTH + 2]={0};

Bit8u * Composite_Process(cga_t *cga, Bit8u border, Bit32u blocks/*, bool doublewidth*/, Bit8u *TempLine)
{
	int x;
	Bit32u x2;

        int w = blocks*4;

#define COMPOSITE_CONVERT(I, Q) do { \
        i[1] = (i[1]<<3) - ap[1]; \
        a = ap[0]; \
        b = bp[0]; \
        c = i[0]+i[0]; \
        d = i[-1]+i[1]; \
        y = ((c+d)<<8) + video_sharpness*(c-d); \
        rr = y + video_ri*(I) + video_rq*(Q); \
        gg = y + video_gi*(I) + video_gq*(Q); \
        bb = y + video_bi*(I) + video_bq*(Q); \
        ++i; \
        ++ap; \
        ++bp; \
        *srgb = (byte_clamp(rr)<<16) | (byte_clamp(gg)<<8) | byte_clamp(bb); \
        ++srgb; \
} while (0)

#define OUT(v) do { *o = (v); ++o; } while (0)

        // Simulate CGA composite output
        int* o = temp;
        Bit8u* rgbi = TempLine;
        int* b = &CGA_Composite_Table[border*68];
        for (x = 0; x < 4; ++x)
                OUT(b[(x+3)&3]);
        OUT(CGA_Composite_Table[(border<<6) | ((*rgbi)<<2) | 3]);
        for (x = 0; x < w-1; ++x) {
                OUT(CGA_Composite_Table[(rgbi[0]<<6) | (rgbi[1]<<2) | (x&3)]);
                ++rgbi;
        }
        OUT(CGA_Composite_Table[((*rgbi)<<6) | (border<<2) | 3]);
        for (x = 0; x < 5; ++x)
                OUT(b[x&3]);

        if ((cga->cgamode & 4) != 0 || !cga_color_burst) {
                // Decode
                int* i = temp + 5;
                Bit32u* srgb = (Bit32u *)TempLine;
                for (x2 = 0; x2 < blocks*4; ++x2) {
                        int c = (i[0]+i[0])<<3;
                        int d = (i[-1]+i[1])<<3;
                        int y = ((c+d)<<8) + video_sharpness*(c-d);
                        ++i;
                        *srgb = byte_clamp(y)*0x10101;
                        ++srgb;
                }
        }
        else {
                // Store chroma
                int* i = temp + 4;
                int* ap = atemp + 1;
                int* bp = btemp + 1;
                for (x = -1; x < w + 1; ++x) {
                        ap[x] = i[-4]-((i[-2]-i[0]+i[2])<<1)+i[4];
                        bp[x] = (i[-3]-i[-1]+i[1]-i[3])<<1;
                        ++i;
                }

                // Decode
                i = temp + 5;
                i[-1] = (i[-1]<<3) - ap[-1];
                i[0] = (i[0]<<3) - ap[0];
                Bit32u* srgb = (Bit32u *)TempLine;
                for (x2 = 0; x2 < blocks; ++x2) {
                        int y,a,b,c,d,rr,gg,bb;
                        COMPOSITE_CONVERT(a, b);
                        COMPOSITE_CONVERT(-b, a);
                        COMPOSITE_CONVERT(-a, -b);
                        COMPOSITE_CONVERT(b, -a);
                }
        }
#undef COMPOSITE_CONVERT
#undef OUT

#if 0
	df = fopen("temp.dmp", "ab");
	fwrite(temp, SCALER_MAXWIDTH + 10, sizeof(int), df);
	fclose(df);
	df = fopen("atemp.dmp", "ab");
	fwrite(atemp, SCALER_MAXWIDTH + 2, sizeof(int), df);
	fclose(df);
	df = fopen("btemp.dmp", "ab");
	fwrite(btemp, SCALER_MAXWIDTH + 2, sizeof(int), df);
	fclose(df);
#endif

        return TempLine;
}

void IncreaseHue(cga_t *cga)
{
	hue_offset += 5.0;

	update_cga16_color(cga);
}

void DecreaseHue(cga_t *cga)
{
	hue_offset -= 5.0;

	update_cga16_color(cga);
}

void IncreaseSaturation(cga_t *cga)
{
	saturation += 5;

	update_cga16_color(cga);
}

void DecreaseSaturation(cga_t *cga)
{
	saturation -= 5;

	update_cga16_color(cga);
}

void IncreaseContrast(cga_t *cga)
{
	contrast += 5;

	update_cga16_color(cga);
}

void DecreaseContrast(cga_t *cga)
{
	contrast -= 5;

	update_cga16_color(cga);
}

void IncreaseBrightness(cga_t *cga)
{
	brightness += 5;

	update_cga16_color(cga);
}

void DecreaseBrightness(cga_t *cga)
{
	brightness -= 5;

	update_cga16_color(cga);
}

void IncreaseSharpness(cga_t *cga)
{
	sharpness += 10;

	update_cga16_color(cga);
}

void DecreaseSharpness(cga_t *cga)
{
	sharpness -= 10;

	update_cga16_color(cga);
}

void cga_comp_init(cga_t *cga)
{
	new_cga = (gfxcard == GFX_NEW_CGA);

	/* Making sure this gets reset after reset. */
	brightness = 0;
	contrast = 100;
	saturation = 100;
	sharpness = 0;
	hue_offset = 0;

	update_cga16_color(cga);
}