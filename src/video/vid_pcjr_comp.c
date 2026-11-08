/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM PCjr composite artifact colour emulation for the 320x200x4
 *          and 640x200x2 graphics modes, ported from DOSBox Staging's
 *          update_cga16_color_pcjr() and VGA_Draw_CGA16_Line().
 *
 *          The arithmetic is kept in the same order and precision as the
 *          original so that the generated colours match it exactly.
 *
 * Authors: reenigne, NewRisingSun, DOSBox Staging Team
 *
 *          Copyright 2002-2021 DOSBox Team.
 *          Copyright 2020-2026 DOSBox Staging Team.
 */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <86box/vid_pcjr_comp.h>

static float
clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((hi < v) ? hi : v);
}

static float
get_rgbi_coefficient(int new_era, uint8_t rgbi)
{
    const float r_coefficient = new_era ? 0.10f : 0.0f;
    const float g_coefficient = new_era ? 0.22f : 0.0f;
    const float b_coefficient = new_era ? 0.07f : 0.0f;
    const float i_coefficient = new_era ? 0.32f : 0.28f;

    const float r = (rgbi & 4) ? r_coefficient : 0.0f;
    const float g = (rgbi & 2) ? g_coefficient : 0.0f;
    const float b = (rgbi & 1) ? b_coefficient : 0.0f;
    const float i = (rgbi & 8) ? i_coefficient : 0.0f;

    return r + g + b + i;
}

#define GAMMA 2.2f

static float
normalize_and_apply_gamma(float v)
{
    const float normalized = clampf((v - 0.075f) / (1 - 0.075f), 0.0f, 1.0f);

    return powf(normalized, GAMMA);
}

static uint8_t
to_linear_rgb(float v)
{
    float linear;
    long  rounded;

    /* Only operate on reasonably positive values. */
    if (!isnormal(v) || (v <= 0.0f))
        return 0;

    /* Switch to linear RGB space and scale to the 8-bit range. */
    linear  = powf(v, 1.0f / GAMMA) * 255;
    rounded = lroundf(linear);

    return (rounded < 0) ? 0 : ((rounded > 255) ? 255 : (uint8_t) rounded);
}

void
pcjr_comp_generate(uint32_t *lut, int new_era, int bw, int bpp1, int hue,
                   int brightness, int contrast, int saturation,
                   const uint8_t *colors)
{
    const float tau = 6.28318531f;       /* 2 * pi */
    const float ns  = 567.0f / 440.0f;   /* Degrees of hue shift per nanosecond. */

    const float tv_brightness = (float) brightness / 100.0f;
    const float tv_saturation = (float) saturation / 100.0f;
    const float tv_contrast   = (1 - tv_brightness) * (float) contrast / 100.0f;

    /* The pixel clock delay calculation is not accurate for 2bpp, but the
       difference is small and a more accurate calculation would be too slow. */
    const float rgbi_pixel_delay = 15.5f * ns;

    /* Delay of white (the overscan colour used for the calculation): XOR on
       the falling but not the rising edge. */
    const uint8_t overscan = 15;
    const float   cp_d     = 39.5f * ns;

    const float phases[6] = {
        270.0f - 21.5f * ns, /* Blue */
        135.0f - 29.5f * ns, /* Green */
        180.0f - 21.5f * ns, /* Cyan */
        000.0f - 21.5f * ns, /* Red */
        315.0f - 29.5f * ns, /* Magenta */
        090.0f - 21.5f * ns  /* Yellow/burst */
    };

    /* All the duty cycle fractions are the same, just under 0.5 as the rising
       edge is delayed 2ns more than the falling edge. */
    const float duty = 0.5f - 2 * ns / 360.0f;

    float rgbi_coefficients[16];
    float chroma_signals[8][4];
    float rgbi_d;
    float chroma_coefficient;
    float burst_delay;
    float color_delay;
    float pixel_clock_delay;
    float hue_adjust;
    float a;
    float b;
    float c;
    float d;

    for (uint8_t i = 0; i < 16; i++)
        rgbi_coefficients[i] = get_rgbi_coefficient(new_era, i);

    rgbi_d             = rgbi_coefficients[overscan];
    chroma_coefficient = new_era ? 0.29f : 0.72f;
    burst_delay        = 60.0f * ns;
    color_delay        = bpp1 ? 0.0f : 25.0f * ns;

    pixel_clock_delay = (cp_d * chroma_coefficient + rgbi_pixel_delay * rgbi_d) /
                        (chroma_coefficient + rgbi_d) + burst_delay + color_delay;

    hue_adjust = (-(90.0f - 33.0f) - (float) hue + pixel_clock_delay) * tau / 360.0f;

    /* We have a rectangle wave with period 1 (in units of the reciprocal of
       the colour burst frequency), duty cycle fraction "duty" and some phase.
       We band-limit this wave to frequency 2 and sample it at intervals of
       1/4, modelling it with 4 frequency components:
         f(x) = a + b*sin(x*tau) + c*cos(x*tau) + d*sin(x*2*tau)
       Then:
         a =   integral(0, 1, f(x)*dx) = duty
         b = 2*integral(0, duty, sin(x*tau)*dx) = 2*(1-cos(duty*tau))/tau
         c = 2*integral(0, duty, cos(x*tau)*dx) = 2*sin(duty*tau)/tau
         d = 2*integral(0, duty, sin(x*2*tau)*dx) = 2*(1-cos(2*tau*duty))/(2*tau) */
    a = duty;
    b = 2.0f * (1.0f - cosf(duty * tau)) / tau;
    c = 2.0f * sinf(duty * tau) / tau;
    d = 2.0f * (1.0f - cosf(duty * 2 * tau)) / (2 * tau);

    for (uint8_t i = 0; i < 4; i++) {
        chroma_signals[0][i] = 0;
        chroma_signals[7][i] = 1;
        for (uint8_t j = 0; j < 6; j++) {
            const float x = (phases[j] + 21.5f * ns + pixel_clock_delay) / 360.0f + i / 4.0f;

            chroma_signals[j + 1][i] = a + b * sinf(x * tau) + c * cosf(x * tau) + d * sinf(x * 2 * tau);
        }
    }

    memset(lut, 0x00, 256 * sizeof(uint32_t));

    for (uint8_t x = 0; x < 4; x++) { /* Position of the pixel in question. */
        const int even = !(x & 1);

        for (uint8_t bits = 0; bits < (even ? 0x10 : 0x40); bits++) {
            float   Y = 0.0f;
            float   I = 0.0f;
            float   Q = 0.0f;
            float   R;
            float   G;
            float   B;
            uint8_t index;

            for (uint8_t p = 0; p < 4; p++) { /* Position within the colour carrier cycle. */
                uint8_t rgbi;
                uint8_t col;
                float   chroma;
                float   composite;

                /* Generate the pixel pattern. */
                if (bpp1)
                    rgbi = ((bits >> (3 - p)) & (even ? 1 : 2)) ? overscan : 0;
                else if (even)
                    rgbi = colors[(bits >> (2 - (p & 2))) & 3];
                else
                    rgbi = colors[(bits >> (4 - ((p + 1) & 6))) & 3];
                rgbi &= 0x0f;

                col = (bw && (rgbi & 7)) ? 7 : (rgbi & 7);

                /* Calculate the composite output. */
                chroma    = chroma_signals[col][(p + x) & 3] * chroma_coefficient;
                composite = chroma + rgbi_coefficients[rgbi];

                Y += composite;
                if (!bw) { /* Burst on. */
                    I += composite * 2 * cosf(hue_adjust + (p + x) * tau / 4.0f);
                    Q += composite * 2 * sinf(hue_adjust + (p + x) * tau / 4.0f);
                }
            }

            Y = clampf(tv_brightness + tv_contrast * Y / 4.0f, 0.0f, 1.0f);
            I = clampf(tv_saturation * tv_contrast * I / 4.0f, -0.5957f, 0.5957f);
            Q = clampf(tv_saturation * tv_contrast * Q / 4.0f, -0.5226f, 0.5226f);

            R = normalize_and_apply_gamma(Y + 0.9563f * I + 0.6210f * Q);
            G = normalize_and_apply_gamma(Y - 0.2721f * I - 0.6474f * Q);
            B = normalize_and_apply_gamma(Y - 1.1069f * I + 1.7046f * Q);

            index      = bits | (even ? 0x30 : 0x80) | ((x & 2) ? 0x00 : 0x40);
            lut[index] = (to_linear_rgb(1.5073f * R - 0.3725f * G - 0.0832f * B) << 16) |
                         (to_linear_rgb(-0.0275f * R + 0.9350f * G + 0.0670f * B) << 8) |
                         to_linear_rgb(-0.0272f * R - 0.0401f * G + 1.1677f * B);
        }
    }
}

void
pcjr_comp_line(const uint32_t *lut, const uint8_t *raw, int len, uint32_t *out)
{
    /* The colour of an even hdot depends on 4 bits of video RAM and that of
       an odd hdot on 6, so each pair of hdots is coloured by its own two bits
       and those of its neighbours. The base indices select the position in
       the colour carrier cycle; bits beyond either end of the line are 0. */
    static const uint8_t phase_base[4] = { 0x30, 0x80, 0x70, 0xc0 };
    const int            groups        = len << 2;
    unsigned             left          = 0;
    unsigned             center        = (len > 0) ? (raw[0] >> 6) : 0;
    unsigned             right;

    for (int k = 0; k < groups; k++) {
        right = ((k + 1) < groups) ? ((raw[(k + 1) >> 2] >> (6 - (((k + 1) & 3) << 1))) & 3) : 0;

        out[k << 1]       = lut[phase_base[(k << 1) & 3] | (left << 2) | center];
        out[(k << 1) + 1] = lut[phase_base[((k << 1) + 1) & 3] | (left << 4) | (center << 2) | right];

        left   = center;
        center = right;
    }
}
