#define X87_TAG_VALID   0
#define X87_TAG_ZERO    1
#define X87_TAG_INVALID 2
#define X87_TAG_EMPTY   3

uint16_t x87_initial_control_word(void);

extern uint32_t x87_pc_off;
extern uint32_t x87_op_off;
extern uint16_t x87_pc_seg;
extern uint16_t x87_op_seg;

static __inline void
x87_set_mmx(void)
{
    uint64_t *p;
    if (fpu_softfloat) {
        fpu_state.tag = 0;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
    } else {
        cpu_state.TOP = 0;
        p             = (uint64_t *) cpu_state.tag;
        /* Every tag valid: TAG_VALID, or the old recompiler's raw 00b */
        *p = 0x0101010101010101ULL;
    }
    cpu_state.ismmx = 1;
}

/* Control instructions leave FIP/FCS/FOP/FDP/FDS as they were: FLDENV,
   FLDCW, FNSTENV, FNSTCW, FRSTOR, FNSAVE, FNSTSW, FNCLEX, FNINIT and the
   FENI/FDISI/FSETPM no-ops. op is (ESC & 7) << 8 | ModR/M. */
static __inline int
x87_op_is_control(uint16_t op)
{
    if ((op & 0xff) < 0xc0) {
        switch (op & 0x738) {
            case 0x120: /* D9 /4 FLDENV */
            case 0x128: /* D9 /5 FLDCW */
            case 0x130: /* D9 /6 FNSTENV */
            case 0x138: /* D9 /7 FNSTCW */
            case 0x520: /* DD /4 FRSTOR */
            case 0x530: /* DD /6 FNSAVE */
            case 0x538: /* DD /7 FNSTSW */
                return 1;
            default:
                return 0;
        }
    }
    switch (op & 0x7ff) {
        case 0x3e0: /* FENI */
        case 0x3e1: /* FDISI */
        case 0x3e2: /* FNCLEX */
        case 0x3e3: /* FNINIT */
        case 0x3e4: /* FSETPM */
        case 0x7e0: /* FNSTSW AX */
            return 1;
        default:
            return 0;
    }
}

/* FPUs earlier than the 487 preserve exception pointers on initialization. */
static __inline void
x87_clear_pointers(void)
{
    if (fpu_type > FPU_NONE && fpu_type < FPU_487SX)
        return;
    if (fpu_softfloat) {
        fpu_state.foo = 0;
        fpu_state.fds = 0;
        fpu_state.fdp = 0;
        fpu_state.fcs = 0;
        fpu_state.fip = 0;
    }
    cpu_state.fpu_op = 0;
    cpu_state.fpu_CS = 0;
    cpu_state.fpu_cs = 0;
    cpu_state.fpu_pc = 0;
    cpu_state.fpu_DS = 0;
    cpu_state.fpu_ds = 0;
    cpu_state.fpu_ea = 0;
}

static __inline void
x87_emms(void)
{
    uint64_t *p;
    if (fpu_softfloat) {
        fpu_state.tag = 0xffff;
        fpu_state.tos = 0;
    } else {
        p  = (uint64_t *) cpu_state.tag;
        /* Every tag empty: TAG_EMPTY, or the old recompiler's raw 11b */
        *p = 0;
        cpu_state.TOP = 0; /* as the softfloat path */
    }
    cpu_state.ismmx = 0;
}

uint16_t x87_gettag(void);
void     x87_settag(uint16_t new_tag);

#define TAG_EMPTY 0
#define TAG_VALID (1 << 0)
/*Hack for FPU copy. If set then MM[].q contains the 64-bit integer loaded by FILD*/
#define TAG_UINT64 (1 << 7)

/*Old dynarec stuff.*/
#define TAG_NOT_UINT64       0xfb

/* ST(i) empty: an empty register's tag is TAG_EMPTY, any other's has
   TAG_VALID set (TAG_VALID, or TAG_VALID | TAG_UINT64), which the
   recompiler's stack-fault check tests alone (codegen_fpu_guard_stack) */
#define X87_ST_EMPTY(i) (cpu_state.tag[(cpu_state.TOP + (i)) & 7] == TAG_EMPTY)

#define X87_ROUNDING_NEAREST 0
#define X87_ROUNDING_DOWN    1
#define X87_ROUNDING_UP      2
#define X87_ROUNDING_CHOP    3

void codegen_set_rounding_mode(int mode);

/* Status Word */
#define FPU_SW_Backward        (0x8000)  /* backward compatibility */
#define FPU_SW_C3              (0x4000)  /* condition bit 3 */
#define FPU_SW_Top             (0x3800)  /* top of stack */
#define FPU_SW_C2              (0x0400)  /* condition bit 2 */
#define FPU_SW_C1              (0x0200)  /* condition bit 1 */
#define FPU_SW_C0              (0x0100)  /* condition bit 0 */
#define FPU_SW_Summary         (0x0080)  /* exception summary */
#define FPU_SW_Stack_Fault     (0x0040)  /* stack fault */
#define FPU_SW_Precision       (0x0020)  /* loss of precision */
#define FPU_SW_Underflow       (0x0010)  /* underflow */
#define FPU_SW_Overflow        (0x0008)  /* overflow */
#define FPU_SW_Zero_Div        (0x0004)  /* divide by zero */
#define FPU_SW_Denormal_Op     (0x0002)  /* denormalized operand */
#define FPU_SW_Invalid         (0x0001)  /* invalid operation */

#define FPU_SW_CC (FPU_SW_C0|FPU_SW_C1|FPU_SW_C2|FPU_SW_C3)

#define FPU_SW_Exceptions_Mask (0x027f)  /* status word exceptions bit mask */

/* Exception flags: */
#define FPU_EX_Precision    (0x0020)  /* loss of precision */
#define FPU_EX_Underflow    (0x0010)  /* underflow */
#define FPU_EX_Overflow     (0x0008)  /* overflow */
#define FPU_EX_Zero_Div     (0x0004)  /* divide by zero */
#define FPU_EX_Denormal     (0x0002)  /* denormalized operand */
#define FPU_EX_Invalid      (0x0001)  /* invalid operation */

/* Special exceptions: */
#define FPU_EX_Stack_Overflow    (0x0041|FPU_SW_C1)     /* stack overflow */
#define FPU_EX_Stack_Underflow   (0x0041)        /* stack underflow */

/* precision control */
#define FPU_EX_Precision_Lost_Up    (EX_Precision | SW_C1)
#define FPU_EX_Precision_Lost_Dn    (EX_Precision)

#define setcc(cc)  \
  fpu_state.swd = (fpu_state.swd & ~(FPU_SW_CC)) | ((cc) & FPU_SW_CC)

#define clear_C1() { fpu_state.swd &= ~FPU_SW_C1; }
#define clear_C2() { fpu_state.swd &= ~FPU_SW_C2; }

/* ************ */
/* Control Word */
/* ************ */

#ifdef FPU_8087
/* The 80187 keeps the reserved bit mask used by later coprocessors. */
#    define FPU_CW_Reserved_Bits ((fpu_type == FPU_80187) ? 0xe0c0 : 0xe040)
#else
#    define FPU_CW_Reserved_Bits (0xe0c0) /* reserved bits */
#endif

#define FPU_CW_Inf		(0x1000)  /* infinity control, legacy */

#define FPU_CW_RC		(0x0C00)  /* rounding control */
#define FPU_CW_PC		(0x0300)  /* precision control */

#define FPU_RC_RND		(0x0000)  /* rounding control */
#define FPU_RC_DOWN		(0x0400)
#define FPU_RC_UP		(0x0800)
#define FPU_RC_CHOP		(0x0C00)

#define FPU_CW_Precision	(0x0020)  /* loss of precision mask */
#define FPU_CW_Underflow	(0x0010)  /* underflow mask */
#define FPU_CW_Overflow		(0x0008)  /* overflow mask */
#define FPU_CW_Zero_Div		(0x0004)  /* divide by zero mask */
#define FPU_CW_Denormal		(0x0002)  /* denormalized operand mask */
#define FPU_CW_Invalid		(0x0001)  /* invalid operation mask */

#define FPU_CW_Exceptions_Mask 	(0x003f)  /* all masks */

/* Precision control bits affect only the following:
   ADD, SUB(R), MUL, DIV(R), and SQRT */
#define FPU_PR_32_BITS          (0x000)
#define FPU_PR_RESERVED_BITS    (0x100)
#define FPU_PR_64_BITS          (0x200)
#define FPU_PR_80_BITS          (0x300)

#include "softfloat3e/softfloat.h"

static __inline int
is_IA_masked(void)
{
    return (fpu_state.cwd & FPU_CW_Invalid);
}

struct softfloat_status_t i387cw_to_softfloat_status_word(uint16_t control_word);
uint16_t              FPU_exception(uint32_t fetchdat, uint16_t exceptions, int store);
int                   FPU_status_word_flags_fpu_compare(int float_relation);
void                  FPU_write_eflags_fpu_compare(int float_relation);
void                  FPU_stack_overflow(uint32_t fetchdat);
void                  FPU_stack_underflow(uint32_t fetchdat, int stnr, int pop_stack);
int                   FPU_handle_NaN32(extFloat80_t a, float32 b, extFloat80_t *r, struct softfloat_status_t *status);
int                   FPU_handle_NaN64(extFloat80_t a, float64 b, extFloat80_t *r, struct softfloat_status_t *status);
int                   FPU_tagof(const extFloat80_t reg);
uint8_t               pack_FPU_TW(uint16_t twd);
uint16_t              unpack_FPU_TW(uint16_t tag_byte);

static __inline uint16_t
i387_get_control_word(void)
{
    return (fpu_state.cwd);
}

static __inline uint16_t
i387_get_status_word(void)
{
    return (fpu_state.swd & ~FPU_SW_Top & 0xFFFF) | ((fpu_state.tos << 11) & FPU_SW_Top);
}

#define IS_TAG_EMPTY(i) \
    (FPU_gettagi(i) == X87_TAG_EMPTY)

static __inline int
FPU_gettagi(int stnr)
{
    return (fpu_state.tag >> (((stnr + fpu_state.tos) & 7) * 2)) & 3;
}

static __inline void
FPU_settagi_valid(int stnr)
{
    int regnr = (stnr + fpu_state.tos) & 7;
    fpu_state.tag &= ~(3 << (regnr * 2)); // FPU_Tag_Valid == '00
}

static __inline void
FPU_settagi(int tag, int stnr)
{
    int regnr = (stnr + fpu_state.tos) & 7;
    fpu_state.tag &= ~(3 << (regnr * 2));
    fpu_state.tag |= (tag & 3) << (regnr * 2);
}

static __inline void
FPU_push(void)
{
    fpu_state.tos = (fpu_state.tos - 1) & 7;
}

static __inline void
FPU_pop(void)
{
    fpu_state.tag |= (3 << (fpu_state.tos * 2));
    fpu_state.tos = (fpu_state.tos + 1) & 7;
}

static __inline extFloat80_t
FPU_read_regi(int stnr)
{
    return fpu_state.st_space[(stnr + fpu_state.tos) & 7];
}

// it is only possible to read FPU tag word through certain
// instructions like FNSAVE, and they update tag word to its
// real value anyway
static __inline void
FPU_save_regi(extFloat80_t reg, int stnr)
{
    fpu_state.st_space[(stnr + fpu_state.tos) & 7] = reg;
    FPU_settagi_valid(stnr);
}

static __inline void
FPU_save_regi_tag(extFloat80_t reg, int tag, int stnr)
{
    fpu_state.st_space[(stnr + fpu_state.tos) & 7] = reg;
    FPU_settagi(tag, stnr);
}

#ifdef FPU_8087
#define FPU_check_pending_exceptions()        \
    do {                                      \
        if ((fpu_state.swd & FPU_SW_Summary) && !(fpu_state.cwd & FPU_SW_Summary)) { \
            fpu_int_set(1);                   \
            return 1;                         \
        }                                     \
    } while (0)
#else
/* A waiting instruction with an exception pending: with CR0.NE, #MF
   with the frame at the instruction. Without it, FERR# raises IRQ 13
   and the instruction waits (is restarted) until the handler clears the
   error or writes port F0h, which asserts IGNNE# until ES clears. */
#define FPU_check_pending_exceptions()          \
    do {                                        \
        if (!(fpu_state.swd & FPU_SW_Summary))  \
            fpu_ignne = 0;                      \
        else if (cr0 & 0x20) {                  \
            cpu_state.pc = cpu_state.oldpc;     \
            new_ne       = 1;                   \
            return 1;                           \
        } else if (!fpu_ignne) {                \
            cpu_state.pc = cpu_state.oldpc;     \
            x87_ferr_assert();                  \
            return 1;                           \
        }                                       \
    } while (0)
#endif

/* A softfloat store's status once its write is done, and FERR# for an
   exception it raised (FPU_exception); a faulting write keeps both as they
   were */
#define FPU_store_done(sw)            \
    do {                              \
        fpu_state.swd = (sw);         \
        x87_ferr_store_done();        \
    } while (0)
/* Shared legacy division: classification precedes destination mutation. */
int x87_legacy_divide(double *dest, double numerator, double denominator, int empty);

int x87_legacy_pending(void);
int x87_legacy_pending_entry(void);

void x87_ferr_assert(void);
void x87_ferr_raised(int was_es, uint16_t unmasked, uint16_t op);
void x87_ferr_nowait(uint16_t op);
void x87_ferr_clear(void);
void x87_ferr_loaded(int was_es, int es_now);
void x87_ferr_store_hold(int was_es, uint16_t unmasked);
void x87_ferr_store_done(void);
void x87_legacy_status_es(int was_es, uint16_t op);
void x87_sf_status_es(int was_es, uint16_t op);

/* The non-waiting forms may inspect or clear a pending exception. */
static __inline int
x87_op_is_nowait(uint16_t op)
{
    if ((op & 0xff) < 0xc0) {
        unsigned group = op & 0x738;
        return group == 0x130 || group == 0x138 || group == 0x530 || group == 0x538;
    }
    return (op >= 0x3e0 && op <= 0x3e4) || op == 0x7e0;
}
