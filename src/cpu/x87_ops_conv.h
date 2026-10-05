#define BIAS80 16383
#define BIAS64 1023

typedef struct {
    int16_t begin;
    union {
        double   d;
        uint64_t ll;
    } eind;
} x87_conv_t;

/* An 80-bit extended value to the nearest double, as the x87's own FST m64
   rounds it: round to nearest even, a mantissa that rounds up to the next
   power of two carries into the exponent, too large an exponent is infinity
   and too small a one goes through the double denormals to zero. NaNs keep
   their sign and the top of their payload, and come out quiet. */
static __inline double
x87_from80(x87_conv_t *test)
{
    const uint64_t sign = (test->begin & 0x8000) ? 0x8000000000000000ULL : 0;
    const int      e80  = test->begin & 0x7fff;
    uint64_t       mant = test->eind.ll;
    int            e;
    uint64_t       m;
    uint64_t       rb;
    uint64_t       half;
    int            sh;

    if (e80 == 0x7fff) {
        if ((mant & 0x7fffffffffffffffULL) == 0)
            test->eind.ll = sign | 0x7ff0000000000000ULL;
        else
            test->eind.ll = sign | 0x7ff8000000000000ULL | ((mant >> 11) & 0x000fffffffffffffULL);
        return test->eind.d;
    }
    if (mant == 0) {
        test->eind.ll = sign;
        return test->eind.d;
    }

    /* The unbiased exponent of the leading one: an unnormal or a denormal
       has it below bit 63. */
    e = ((e80 == 0) ? 1 : e80) - 16383;
    while (!(mant & 0x8000000000000000ULL)) {
        mant <<= 1;
        e--;
    }
    if (e > 1023) {
        test->eind.ll = sign | 0x7ff0000000000000ULL;
        return test->eind.d;
    }

    /* 53 bits for a normal double; fewer below 2^-1022, down to none. */
    sh = 11 + ((e < -1022) ? (-1022 - e) : 0);
    if (sh > 64) {
        test->eind.ll = sign;
        return test->eind.d;
    }
    m    = (sh == 64) ? 0 : (mant >> sh);
    rb   = (sh == 64) ? mant : (mant & ((1ULL << sh) - 1));
    half = 1ULL << (sh - 1);
    if ((rb > half) || ((rb == half) && (m & 1)))
        m++;

    if (e < -1022) {
        /* A denormal; one that rounded up to 2^-1022 is the smallest
           normal, which the same bits already say. */
        test->eind.ll = sign | m;
    } else {
        if (m == (1ULL << 53)) {
            m >>= 1;
            e++;
            if (e > 1023) {
                test->eind.ll = sign | 0x7ff0000000000000ULL;
                return test->eind.d;
            }
        }
        test->eind.ll = sign | ((uint64_t) (e + 1023) << 52) | (m & 0x000fffffffffffffULL);
    }
    return test->eind.d;
}

/* A double to 80-bit extended, exactly: the explicit integer bit set on
   every normal value, denormal doubles normalized (the 80-bit exponent
   reaches far below them), infinities and NaNs with exponent 7FFF. */
static __inline void
x87_to80(double d, x87_conv_t *test)
{
    uint64_t sign;
    int      e;
    uint64_t mant;

    test->eind.d = d;
    sign         = test->eind.ll >> 63;
    e            = (int) ((test->eind.ll >> 52) & 0x7ff);
    mant         = test->eind.ll & 0x000fffffffffffffULL;

    if (e == 0x7ff) {
        /* A NaN comes out quiet, as FLD m64 makes a signalling one: the
           register cannot say where its NaN came from, and loading a double
           is by far the commoner way to get one. */
        test->begin   = (int16_t) ((sign << 15) | 0x7fff);
        test->eind.ll = 0x8000000000000000ULL | (mant << 11) | (mant ? 0x4000000000000000ULL : 0);
    } else if (e == 0) {
        if (mant == 0) {
            test->begin   = (int16_t) (sign << 15);
            test->eind.ll = 0;
        } else {
            /* A denormal double: normalize it. */
            int e80 = 16383 - 1022;

            mant <<= 11;
            while (!(mant & 0x8000000000000000ULL)) {
                mant <<= 1;
                e80--;
            }
            test->begin   = (int16_t) ((sign << 15) | e80);
            test->eind.ll = mant;
        }
    } else {
        test->begin   = (int16_t) ((sign << 15) | (e - 1023 + 16383));
        test->eind.ll = 0x8000000000000000ULL | (mant << 11);
    }
}
