#define BIAS80 16383
#define BIAS64 1023

typedef struct {
    int16_t begin;
    union {
        double d;
        uint64_t ll;
    } eind;
} x87_conv_t;

static __inline double x87_from80(x87_conv_t *test)
{
        int64_t exp64;
        int64_t blah;
        int64_t exp64final;
        int64_t mant64;
        int64_t sign;

        exp64 = (((test->begin&0x7fff) - BIAS80));
        blah = ((exp64 >0)?exp64:-exp64)&0x3ff;
        exp64final = ((exp64 >0)?blah:-blah) +BIAS64;

        mant64 = (test->eind.ll >> 11) & (0xfffffffffffffll);
        sign = (test->begin&0x8000)?1:0;

        if ((test->begin & 0x7fff) == 0x7fff)
                exp64final = 0x7ff;
        if ((test->begin & 0x7fff) == 0)
                exp64final = 0;
        if (test->eind.ll & 0x400)
                mant64++;

        test->eind.ll = (sign <<63)|(exp64final << 52)| mant64;

        return test->eind.d;
}

static __inline void x87_to80(double d, x87_conv_t *test)
{
        int64_t sign80;
        int64_t exp80;
        int64_t exp80final;
        int64_t mant80;
        int64_t mant80final;

        test->eind.d=d;

        sign80 = (test->eind.ll&(0x8000000000000000ll))?1:0;
        exp80 =  test->eind.ll&(0x7ff0000000000000ll);
        exp80final = (exp80>>52);
        mant80 = test->eind.ll&(0x000fffffffffffffll);
        mant80final = (mant80 << 11);

        if (exp80final == 0x7ff) /*Infinity / Nan*/
        {
                exp80final = 0x7fff;
                mant80final |= (0x8000000000000000ll);
        }
        else if (d != 0){  /* Zero is a special case */
                /* Elvira wants the 8 and tcalc doesn't */
                mant80final |= (0x8000000000000000ll);
                /* Ca-cyber doesn't like this when result is zero. */
                exp80final += (BIAS80 - BIAS64);
        }
        test->begin = (((int16_t)sign80)<<15)| (int16_t)exp80final;
        test->eind.ll = mant80final;
}
