#ifndef EMU_FILTERS_H
# define EMU_FILTERS_H

#define NCoef 2

/* fc=150Hz */
static inline float adgold_highpass_iir(int i, float NewSample) {
    float ACoef[NCoef+1] = {
        0.98657437157334349000,
        -1.97314874314668700000,
        0.98657437157334349000
    };

    float BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.97223372919758360000,
        0.97261396931534050000
    };

    static float y[2][NCoef+1]; /* output samples */
    static float x[2][NCoef+1]; /* input samples */
    int n;

    /* shift the old samples */
    for(n=NCoef; n>0; n--) {
       x[i][n] = x[i][n-1];
       y[i][n] = y[i][n-1];
    }

    /* Calculate the new output */
    x[i][0] = NewSample;
    y[i][0] = ACoef[0] * x[i][0];
    for(n=1; n<=NCoef; n++)
        y[i][0] += ACoef[n] * x[i][n] - BCoef[n] * y[i][n];

    return y[i][0];
}

/* fc=150Hz */
static inline float adgold_lowpass_iir(int i, float NewSample) {
    float ACoef[NCoef+1] = {
        0.00009159473951071446,
        0.00018318947902142891,
        0.00009159473951071446
    };

    float BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.97223372919526560000,
        0.97261396931306277000
    };

    static float y[2][NCoef+1]; /* output samples */
    static float x[2][NCoef+1]; /* input samples */
    int n;

    /* shift the old samples */
    for(n=NCoef; n>0; n--) {
       x[i][n] = x[i][n-1];
       y[i][n] = y[i][n-1];
    }

    /* Calculate the new output */
    x[i][0] = NewSample;
    y[i][0] = ACoef[0] * x[i][0];
    for(n=1; n<=NCoef; n++)
        y[i][0] += ACoef[n] * x[i][n] - BCoef[n] * y[i][n];

    return y[i][0];
}

/* fc=56Hz */
static inline float adgold_pseudo_stereo_iir(float NewSample) {
    float ACoef[NCoef+1] = {
        0.00001409030866231767,
        0.00002818061732463533,
        0.00001409030866231767
    };

    float BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.98733021473466760000,
        0.98738361004063568000
    };

    static float y[NCoef+1]; /* output samples */
    static float x[NCoef+1]; /* input samples */
    int n;

    /* shift the old samples */
    for(n=NCoef; n>0; n--) {
       x[n] = x[n-1];
       y[n] = y[n-1];
    }

    /* Calculate the new output */
    x[0] = NewSample;
    y[0] = ACoef[0] * x[0];
    for(n=1; n<=NCoef; n++)
        y[0] += ACoef[n] * x[n] - BCoef[n] * y[n];

    return y[0];
}

/* fc=3.2kHz - probably incorrect */
static inline float dss_iir(float NewSample) {
    float ACoef[NCoef+1] = {
        0.03356837051492005100,
        0.06713674102984010200,
        0.03356837051492005100
    };

    float BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.41898265221812010000,
        0.55326988968868285000
    };

    static float y[NCoef+1]; /* output samples */
    static float x[NCoef+1]; /* input samples */
    int n;

    /* shift the old samples */
    for(n=NCoef; n>0; n--) {
       x[n] = x[n-1];
       y[n] = y[n-1];
    }

    /* Calculate the new output */
    x[0] = NewSample;
    y[0] = ACoef[0] * x[0];
    for(n=1; n<=NCoef; n++)
        y[0] += ACoef[n] * x[n] - BCoef[n] * y[n];

    return y[0];
}

#undef NCoef
#define NCoef 1
/*Basic high pass to remove DC bias. fc=10Hz*/
static inline float dac_iir(int i, float NewSample) {
    float ACoef[NCoef+1] = {
        0.99901119820285345000,
        -0.99901119820285345000
    };

    float BCoef[NCoef+1] = {
        1.00000000000000000000,
        -0.99869185905052738000
    };

    static float y[2][NCoef+1]; /* output samples */
    static float x[2][NCoef+1]; /* input samples */
    int n;

    /* shift the old samples */
    for(n=NCoef; n>0; n--) {
       x[i][n] = x[i][n-1];
       y[i][n] = y[i][n-1];
    }

    /* Calculate the new output */
    x[i][0] = NewSample;
    y[i][0] = ACoef[0] * x[i][0];
    for(n=1; n<=NCoef; n++)
        y[i][0] += ACoef[n] * x[i][n] - BCoef[n] * y[i][n];

    return y[i][0];
}


#undef NCoef
#define NCoef 2

/* fc=350Hz */
static inline double low_iir(int c, int i, double NewSample) {
    double ACoef[NCoef+1] = {
        0.00049713569693400649,
        0.00099427139386801299,
        0.00049713569693400649
    };

    double BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.93522955470669530000,
        0.93726236021404663000
    };

    static double y[2][2][NCoef+1]; /* output samples */
    static double x[2][2][NCoef+1]; /* input samples */
    int n;

    /* shift the old samples */
    for(n=NCoef; n>0; n--) {
       x[c][i][n] = x[c][i][n-1];
       y[c][i][n] = y[c][i][n-1];
    }

    /* Calculate the new output */
    x[c][i][0] = NewSample;
    y[c][i][0] = ACoef[0] * x[c][i][0];
    for(n=1; n<=NCoef; n++)
        y[c][i][0] += ACoef[n] * x[c][i][n] - BCoef[n] * y[c][i][n];

    return y[c][i][0];
}

/* fc=350Hz */
static inline double low_cut_iir(int c, int i, double NewSample) {
    double ACoef[NCoef+1] = {
        0.96839970114733542000,
        -1.93679940229467080000,
        0.96839970114733542000
    };

    double BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.93522955471202770000,
        0.93726236021916731000
    };

    static double y[2][2][NCoef+1]; /* output samples */
    static double x[2][2][NCoef+1]; /* input samples */
    int n;

    /* shift the old samples */
    for(n=NCoef; n>0; n--) {
       x[c][i][n] = x[c][i][n-1];
       y[c][i][n] = y[c][i][n-1];
    }

    /* Calculate the new output */
    x[c][i][0] = NewSample;
    y[c][i][0] = ACoef[0] * x[c][i][0];
    for(n=1; n<=NCoef; n++)
        y[c][i][0] += ACoef[n] * x[c][i][n] - BCoef[n] * y[c][i][n];

    return y[c][i][0];
}

/* fc=3.5kHz */
static inline double high_iir(int c, int i, double NewSample) {
    double ACoef[NCoef+1] = {
        0.72248704753064896000,
        -1.44497409506129790000,
        0.72248704753064896000
    };

    double BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.36640781670578510000,
        0.52352474706139873000
    };
    static double y[2][2][NCoef+1]; /* output samples */
    static double x[2][2][NCoef+1]; /* input samples */
    int n;

    /* shift the old samples */
    for(n=NCoef; n>0; n--) {
       x[c][i][n] = x[c][i][n-1];
       y[c][i][n] = y[c][i][n-1];
    }

    /* Calculate the new output */
    x[c][i][0] = NewSample;
    y[c][i][0] = ACoef[0] * x[c][i][0];
    for(n=1; n<=NCoef; n++)
        y[c][i][0] += ACoef[n] * x[c][i][n] - BCoef[n] * y[c][i][n];

    return y[c][i][0];
}

/* fc=3.5kHz */
static inline double high_cut_iir(int c, int i, double NewSample) {
    double ACoef[NCoef+1] = {
        0.03927726802250377400,
        0.07855453604500754700,
        0.03927726802250377400
    };

    double BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.36640781666419950000,
        0.52352474703279628000
    };
    static double y[2][2][NCoef+1]; /* output samples */
    static double x[2][2][NCoef+1]; /* input samples */
    int n;

    /* shift the old samples */
    for(n=NCoef; n>0; n--) {
       x[c][i][n] = x[c][i][n-1];
       y[c][i][n] = y[c][i][n-1];
    }

    /* Calculate the new output */
    x[c][i][0] = NewSample;
    y[c][i][0] = ACoef[0] * x[c][i][0];
    for(n=1; n<=NCoef; n++)
        y[c][i][0] += ACoef[n] * x[c][i][n] - BCoef[n] * y[c][i][n];

    return y[c][i][0];
}

/* fc=5.283kHz, gain=-9.477dB, width=0.4845 */
static inline double deemph_iir(int i, double NewSample) {
    double ACoef[NCoef+1] = {
        0.46035077886318842566,
        -0.28440821191249848754,
        0.03388877229118691936
    };

    double BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.05429146278569141337,
        0.26412280202756849290
    };
    static double y[2][NCoef+1]; /* output samples */
    static double x[2][NCoef+1]; /* input samples */
    int n;

    /* shift the old samples */
    for(n=NCoef; n>0; n--) {
       x[i][n] = x[i][n-1];
       y[i][n] = y[i][n-1];
    }

    /* Calculate the new output */
    x[i][0] = NewSample;
    y[i][0] = ACoef[0] * x[i][0];
    for(n=1; n<=NCoef; n++)
        y[i][0] += ACoef[n] * x[i][n] - BCoef[n] * y[i][n];

    return y[i][0];
}

#undef NCoef
#define NCoef 2

/* fc=3.2kHz */
static inline double sb_iir(int c, int i, double NewSample) {
    double ACoef[NCoef+1] = {
        0.03356837051492005100,
        0.06713674102984010200,
        0.03356837051492005100
    };

    double BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.41898265221812010000,
        0.55326988968868285000
    };

    static double y[2][2][NCoef+1]; /* output samples */
    static double x[2][2][NCoef+1]; /* input samples */
    int n;

    /* shift the old samples */
    for(n=NCoef; n>0; n--) {
       x[c][i][n] = x[c][i][n-1];
       y[c][i][n] = y[c][i][n-1];
    }

    /* Calculate the new output */
    x[c][i][0] = NewSample;
    y[c][i][0] = ACoef[0] * x[c][i][0];
    for(n=1; n<=NCoef; n++)
        y[c][i][0] += ACoef[n] * x[c][i][n] - BCoef[n] * y[c][i][n];

    return y[c][i][0];
}



#undef NCoef
#define NCoef 1
#define SB16_NCoef 51

extern double low_fir_sb16_coef[2][SB16_NCoef];

static inline double low_fir_sb16(int c, int i, double NewSample)
{
        static double x[2][2][SB16_NCoef+1]; //input samples
        static int pos[2] = { 0, 0 };
        double out = 0.0;
        int n;

        /* Calculate the new output */
        x[c][i][pos[c]] = NewSample;

        for (n = 0; n < ((SB16_NCoef+1)-pos[c]) && n < SB16_NCoef; n++)
                out += low_fir_sb16_coef[c][n] * x[c][i][n+pos[c]];
        for (; n < SB16_NCoef; n++)
                out += low_fir_sb16_coef[c][n] * x[c][i][(n+pos[c]) - (SB16_NCoef+1)];

        if (i == 1)
        {
                pos[c]++;
                if (pos[c] > SB16_NCoef)
                        pos[c] = 0;
        }

        return out;
}

#endif /*EMU_FILTERS_H*/
