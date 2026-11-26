#ifndef EMU_FILTERS_H
#define EMU_FILTERS_H

#include <math.h>

#define NCoef 2

/* fc=150Hz */
static inline float
adgold_highpass_iir(int i, float NewSample)
{
    float ACoef[NCoef + 1] = {
        0.98657437157334349000,
        -1.97314874314668700000,
        0.98657437157334349000
    };

    float BCoef[NCoef + 1] = {
        1.00000000000000000000,
        -1.97223372919758360000,
        0.97261396931534050000
    };

    static float y[4][NCoef + 1]; /* output samples */
    static float x[4][NCoef + 1]; /* input samples */
    int          n;

    /* shift the old samples */
    for (n = NCoef; n > 0; n--) {
        x[i][n] = x[i][n - 1];
        y[i][n] = y[i][n - 1];
    }

    /* Calculate the new output */
    x[i][0] = NewSample;
    y[i][0] = ACoef[0] * x[i][0];
    for (n = 1; n <= NCoef; n++)
        y[i][0] += ACoef[n] * x[i][n] - BCoef[n] * y[i][n];

    return y[i][0];
}

/* fc=150Hz */
static inline float
adgold_lowpass_iir(int i, float NewSample)
{
    float ACoef[NCoef + 1] = {
        0.00009159473951071446,
        0.00018318947902142891,
        0.00009159473951071446
    };

    float BCoef[NCoef + 1] = {
        1.00000000000000000000,
        -1.97223372919526560000,
        0.97261396931306277000
    };

    static float y[4][NCoef + 1]; /* output samples */
    static float x[4][NCoef + 1]; /* input samples */
    int          n;

    /* shift the old samples */
    for (n = NCoef; n > 0; n--) {
        x[i][n] = x[i][n - 1];
        y[i][n] = y[i][n - 1];
    }

    /* Calculate the new output */
    x[i][0] = NewSample;
    y[i][0] = ACoef[0] * x[i][0];
    for (n = 1; n <= NCoef; n++)
        y[i][0] += ACoef[n] * x[i][n] - BCoef[n] * y[i][n];

    return y[i][0];
}

/* fc=56Hz */
static inline float
adgold_pseudo_stereo_iir(int i, float NewSample)
{
    float ACoef[NCoef + 1] = {
        0.00001409030866231767,
        0.00002818061732463533,
        0.00001409030866231767
    };

    float BCoef[NCoef + 1] = {
        1.00000000000000000000,
        -1.98733021473466760000,
        0.98738361004063568000
    };

    static float y[2][NCoef + 1]; /* output samples */
    static float x[2][NCoef + 1]; /* input samples */
    int          n;

    /* shift the old samples */
    for (n = NCoef; n > 0; n--) {
        x[i][n] = x[i][n - 1];
        y[i][n] = y[i][n - 1];
    }

    /* Calculate the new output */
    x[i][0] = NewSample;
    y[i][0] = ACoef[0] * x[i][0];
    for (n = 1; n <= NCoef; n++)
        y[i][0] += ACoef[n] * x[i][n] - BCoef[n] * y[i][n];

    return y[i][0];
}

/* fc=3.2kHz - probably incorrect */
static inline float
dss_iir(float NewSample)
{
    float ACoef[NCoef + 1] = {
        0.03356837051492005100,
        0.06713674102984010200,
        0.03356837051492005100
    };

    float BCoef[NCoef + 1] = {
        1.00000000000000000000,
        -1.41898265221812010000,
        0.55326988968868285000
    };

    static float y[NCoef + 1]; /* output samples */
    static float x[NCoef + 1]; /* input samples */
    int          n;

    /* shift the old samples */
    for (n = NCoef; n > 0; n--) {
        x[n] = x[n - 1];
        y[n] = y[n - 1];
    }

    /* Calculate the new output */
    x[0] = NewSample;
    y[0] = ACoef[0] * x[0];
    for (n = 1; n <= NCoef; n++)
        y[0] += ACoef[n] * x[n] - BCoef[n] * y[n];

    return y[0];
}

#undef NCoef
#define NCoef 1
/*Basic high pass to remove DC bias. fc=10Hz*/
static inline float
dac_iir(int i, float NewSample)
{
    float ACoef[NCoef + 1] = {
        0.99901119820285345000,
        -0.99901119820285345000
    };

    float BCoef[NCoef + 1] = {
        1.00000000000000000000,
        -0.99869185905052738000
    };

    static float y[2][NCoef + 1]; /* output samples */
    static float x[2][NCoef + 1]; /* input samples */
    int          n;

    /* shift the old samples */
    for (n = NCoef; n > 0; n--) {
        x[i][n] = x[i][n - 1];
        y[i][n] = y[i][n - 1];
    }

    /* Calculate the new output */
    x[i][0] = NewSample;
    y[i][0] = ACoef[0] * x[i][0];
    for (n = 1; n <= NCoef; n++)
        y[i][0] += ACoef[n] * x[i][n] - BCoef[n] * y[i][n];

    return y[i][0];
}


/* The bass/treble and SB filters are 2nd order Butterworth filters, designed for
   the rate their user runs at, which is set per user with filters_set_rate(). */
#define FILTER_USERS 6

enum {
    FILTER_LOW = 0,
    FILTER_LOW_CUT,
    FILTER_HIGH,
    FILTER_HIGH_CUT,
    FILTER_SB,
    FILTER_KINDS
};

typedef struct filter_coefs_t {
    double rate;
    double a[3];
    double b[3];
} filter_coefs_t;

static double         filter_user_rate[FILTER_USERS] = { 48000.0, 48000.0, 48000.0, 48000.0, 48000.0, 48000.0 };
static filter_coefs_t filter_coefs[FILTER_KINDS][FILTER_USERS];

static inline void
filters_set_rate(const int c, const double rate)
{
    if ((c >= 0) && (c < FILTER_USERS) && (rate > 0.0))
        filter_user_rate[c] = rate;
}

static inline const filter_coefs_t *
filter_get_coefs(const int kind, const int c)
{
    static const double fcs[FILTER_KINDS]  = { 350.0, 350.0, 3500.0, 3500.0, 3200.0 };
    static const int    highs[FILTER_KINDS] = { 0, 1, 1, 0, 0 };
    filter_coefs_t *    f                  = &filter_coefs[kind][c];
    const double        fs                 = filter_user_rate[c];

    if (f->rate != fs) {
        double fc = fcs[kind];
        double w;
        double alpha;
        double cosw;
        double a0;

        if (fc > (0.45 * fs))
            fc = 0.45 * fs;

        w     = 2.0 * 3.14159265358979323846 * fc / fs;
        alpha = sin(w) / (2.0 * 0.70710678118654752440);
        cosw    = cos(w);
        a0    = 1.0 + alpha;

        if (highs[kind]) {
            f->a[0] = ((1.0 + cosw) / 2.0) / a0;
            f->a[1] = -(1.0 + cosw) / a0;
        } else {
            f->a[0] = ((1.0 - cosw) / 2.0) / a0;
            f->a[1] = (1.0 - cosw) / a0;
        }
        f->a[2] = f->a[0];
        f->b[0] = 1.0;
        f->b[1] = (-2.0 * cosw) / a0;
        f->b[2] = (1.0 - alpha) / a0;
        f->rate = fs;
    }

    return f;
}

#undef NCoef
#define NCoef 2

/* fc=350Hz */
static inline double
low_iir(int c, int i, double NewSample)
{
    const filter_coefs_t *coefs = filter_get_coefs(FILTER_LOW, c);
    const double *       ACoef  = coefs->a;
    const double *       BCoef  = coefs->b;

    static double y[6][2][NCoef + 1]; /* output samples */
    static double x[6][2][NCoef + 1]; /* input samples */
    int           n;

    /* shift the old samples */
    for (n = NCoef; n > 0; n--) {
        x[c][i][n] = x[c][i][n - 1];
        y[c][i][n] = y[c][i][n - 1];
    }

    /* Calculate the new output */
    x[c][i][0] = NewSample;
    y[c][i][0] = ACoef[0] * x[c][i][0];
    for (n = 1; n <= NCoef; n++)
        y[c][i][0] += ACoef[n] * x[c][i][n] - BCoef[n] * y[c][i][n];

    return y[c][i][0];
}

/* fc=350Hz */
static inline double
low_cut_iir(int c, int i, double NewSample)
{
    const filter_coefs_t *coefs = filter_get_coefs(FILTER_LOW_CUT, c);
    const double *       ACoef  = coefs->a;
    const double *       BCoef  = coefs->b;

    static double y[6][2][NCoef + 1]; /* output samples */
    static double x[6][2][NCoef + 1]; /* input samples */
    int           n;

    /* shift the old samples */
    for (n = NCoef; n > 0; n--) {
        x[c][i][n] = x[c][i][n - 1];
        y[c][i][n] = y[c][i][n - 1];
    }

    /* Calculate the new output */
    x[c][i][0] = NewSample;
    y[c][i][0] = ACoef[0] * x[c][i][0];
    for (n = 1; n <= NCoef; n++)
        y[c][i][0] += ACoef[n] * x[c][i][n] - BCoef[n] * y[c][i][n];

    return y[c][i][0];
}

/* fc=3.5kHz */
static inline double
high_iir(int c, int i, double NewSample)
{
    const filter_coefs_t *coefs = filter_get_coefs(FILTER_HIGH, c);
    const double *       ACoef  = coefs->a;
    const double *       BCoef  = coefs->b;
    static double y[6][2][NCoef + 1]; /* output samples */
    static double x[6][2][NCoef + 1]; /* input samples */
    int           n;

    /* shift the old samples */
    for (n = NCoef; n > 0; n--) {
        x[c][i][n] = x[c][i][n - 1];
        y[c][i][n] = y[c][i][n - 1];
    }

    /* Calculate the new output */
    x[c][i][0] = NewSample;
    y[c][i][0] = ACoef[0] * x[c][i][0];
    for (n = 1; n <= NCoef; n++)
        y[c][i][0] += ACoef[n] * x[c][i][n] - BCoef[n] * y[c][i][n];

    return y[c][i][0];
}

/* fc=3.5kHz */
static inline double
high_cut_iir(int c, int i, double NewSample)
{
    const filter_coefs_t *coefs = filter_get_coefs(FILTER_HIGH_CUT, c);
    const double *       ACoef  = coefs->a;
    const double *       BCoef  = coefs->b;
    static double y[6][2][NCoef + 1]; /* output samples */
    static double x[6][2][NCoef + 1]; /* input samples */
    int           n;

    /* shift the old samples */
    for (n = NCoef; n > 0; n--) {
        x[c][i][n] = x[c][i][n - 1];
        y[c][i][n] = y[c][i][n - 1];
    }

    /* Calculate the new output */
    x[c][i][0] = NewSample;
    y[c][i][0] = ACoef[0] * x[c][i][0];
    for (n = 1; n <= NCoef; n++)
        y[c][i][0] += ACoef[n] * x[c][i][n] - BCoef[n] * y[c][i][n];

    return y[c][i][0];
}

/* fc=5.283kHz, gain=-9.477dB, width=0.4845 */
static inline double
deemph_iir(int i, double NewSample)
{
    double ACoef[NCoef + 1] = {
        0.46035077886318842566,
        -0.28440821191249848754,
        0.03388877229118691936
    };

    double BCoef[NCoef + 1] = {
        1.00000000000000000000,
        -1.05429146278569141337,
        0.26412280202756849290
    };
    static double y[6][NCoef + 1]; /* output samples */
    static double x[6][NCoef + 1]; /* input samples */
    int           n;

    /* shift the old samples */
    for (n = NCoef; n > 0; n--) {
        x[i][n] = x[i][n - 1];
        y[i][n] = y[i][n - 1];
    }

    /* Calculate the new output */
    x[i][0] = NewSample;
    y[i][0] = ACoef[0] * x[i][0];
    for (n = 1; n <= NCoef; n++)
        y[i][0] += ACoef[n] * x[i][n] - BCoef[n] * y[i][n];

    return y[i][0];
}

#undef NCoef
#define NCoef 2

/* fc=3.2kHz */
static inline double
sb_iir(int c, int i, double NewSample)
{
    const filter_coefs_t *coefs = filter_get_coefs(FILTER_SB, c);
    const double *       ACoef  = coefs->a;
    const double *       BCoef  = coefs->b;

    static double y[6][2][NCoef + 1]; /* output samples */
    static double x[6][2][NCoef + 1]; /* input samples */
    int           n;

    /* shift the old samples */
    for (n = NCoef; n > 0; n--) {
        x[c][i][n] = x[c][i][n - 1];
        y[c][i][n] = y[c][i][n - 1];
    }

    /* Calculate the new output */
    x[c][i][0] = NewSample;
    y[c][i][0] = ACoef[0] * x[c][i][0];
    for (n = 1; n <= NCoef; n++)
        y[c][i][0] += ACoef[n] * x[c][i][n] - BCoef[n] * y[c][i][n];

    return y[c][i][0];
}

#undef NCoef
#define NCoef      1
#define SB16_NCoef 51

extern double low_fir_sb16_coef[SB16_NCoef];

static inline double
low_fir_sb16(int i, double NewSample)
{
    static double x[2][SB16_NCoef + 1]; // input samples
    static int    pos    = 0;
    double        out    = 0.0;
    int           n;

    /* Calculate the new output */
    x[i][pos] = NewSample;

    for (n = 0; n < ((SB16_NCoef + 1) - pos) && n < SB16_NCoef; n++)
        out += low_fir_sb16_coef[n] * x[i][n + pos];
    for (; n < SB16_NCoef; n++)
        out += low_fir_sb16_coef[n] * x[i][(n + pos) - (SB16_NCoef + 1)];

    if (i == 1) {
        pos++;
        if (pos > SB16_NCoef)
            pos = 0;
    }

    return out;
}

extern double low_fir_ess_dac2_coef[SB16_NCoef];

static inline double
low_fir_ess_dac2(int i, double NewSample)
{
    static double x[2][SB16_NCoef + 1]; // input samples
    static int    pos    = 0;
    double        out    = 0.0;
    int           n;

    /* Calculate the new output */
    x[i][pos] = NewSample;

    for (n = 0; n < ((SB16_NCoef + 1) - pos) && n < SB16_NCoef; n++)
        out += low_fir_ess_dac2_coef[n] * x[i][n + pos];
    for (; n < SB16_NCoef; n++)
        out += low_fir_ess_dac2_coef[n] * x[i][(n + pos) - (SB16_NCoef + 1)];

    if (i == 1) {
        pos++;
        if (pos > SB16_NCoef)
            pos = 0;
    }

    return out;
}

extern double low_fir_pas16_coef[SB16_NCoef];

static inline double
low_fir_pas16(const int i, const double NewSample)
{
    static double x[2][SB16_NCoef + 1]; // input samples
    static int    pos = 0;
    double        out = 0.0;
    int           n;

    /* Calculate the new output */
    x[i][pos] = NewSample;

    for (n = 0; n < ((SB16_NCoef + 1) - pos) && n < SB16_NCoef; n++)
        out += low_fir_pas16_coef[n] * x[i][n + pos];
    for (; n < SB16_NCoef; n++)
        out += low_fir_pas16_coef[n] * x[i][(n + pos) - (SB16_NCoef + 1)];

    if (i == 1) {
        pos++;
        if (pos > SB16_NCoef)
            pos = 0;
    }

    return out;
}

#endif /*EMU_FILTERS_H*/
