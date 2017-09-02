#define NCoef 2

/* fc=350Hz */
static inline float low_iir(int i, float NewSample) {
    float ACoef[NCoef+1] = {
        0.00049713569693400649,
        0.00099427139386801299,
        0.00049713569693400649
    };

    float BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.93522955470669530000,
        0.93726236021404663000
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

/* fc=350Hz */
static inline float low_cut_iir(int i, float NewSample) {
    float ACoef[NCoef+1] = {
        0.96839970114733542000,
        -1.93679940229467080000,
        0.96839970114733542000
    };

    float BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.93522955471202770000,
        0.93726236021916731000
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

/* fc=3.5kHz */
static inline float high_iir(int i, float NewSample) {
    float ACoef[NCoef+1] = {
        0.72248704753064896000,
        -1.44497409506129790000,
        0.72248704753064896000
    };

    float BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.36640781670578510000,
        0.52352474706139873000
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

/* fc=3.5kHz */
static inline float high_cut_iir(int i, float NewSample) {
    float ACoef[NCoef+1] = {
        0.03927726802250377400,
        0.07855453604500754700,
        0.03927726802250377400
    };

    float BCoef[NCoef+1] = {
        1.00000000000000000000,
        -1.36640781666419950000,
        0.52352474703279628000
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

/* fc=3.2kHz */
static inline float sb_iir(int i, float NewSample) {
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
    
/*    float ACoef[NCoef+1] = {
        0.17529642630084405000,
        0.17529642630084405000
    };

    float BCoef[NCoef+1] = {
        1.00000000000000000000,
        -0.64940759319751051000
    };*/
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
