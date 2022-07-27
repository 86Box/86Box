typedef struct
{
        int f2xm1;
        int fabs;
        int fadd, fadd_32, fadd_64;
        int fbld;
        int fbstp;
        int fchs;
        int fclex;
        int fcom, fcom_32, fcom_64;
        int fcos;
        int fincdecstp;
        int fdisi_eni;
        int fdiv, fdiv_32, fdiv_64;
        int ffree;
        int fadd_i16, fadd_i32;
        int fcom_i16, fcom_i32;
        int fdiv_i16, fdiv_i32;
        int fild_16, fild_32, fild_64;
        int fmul_i16, fmul_i32;
        int finit;
        int fist_16, fist_32, fist_64;
        int fld, fld_32, fld_64, fld_80;
        int fld_z1, fld_const;
        int fldcw;
        int fldenv;
        int fmul, fmul_32, fmul_64;
        int fnop;
        int fpatan;
        int fprem, fprem1;
        int fptan;
        int frndint;
        int frstor;
        int fsave;
        int fscale;
        int fsetpm;
        int fsin_cos, fsincos;
        int fsqrt;
        int fst, fst_32, fst_64, fst_80;
        int fstcw_sw;
        int fstenv;
        int ftst;
        int fucom;
        int fwait;
        int fxam;
        int fxch;
        int fxtract;
        int fyl2x, fyl2xp1;
} x87_timings_t;

extern const x87_timings_t x87_timings_8087;
extern const x87_timings_t x87_timings_287;
extern const x87_timings_t x87_timings_387;
extern const x87_timings_t x87_timings_486;

extern const x87_timings_t x87_concurrency_486;

extern x87_timings_t x87_timings;
extern x87_timings_t x87_concurrency;
