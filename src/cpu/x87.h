#define X87_TAG_VALID   0
#define X87_TAG_ZERO    1
#define X87_TAG_INVALID 2
#define X87_TAG_EMPTY   3

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
        *p            = 0x0101010101010101ULL;
    }
    cpu_state.ismmx = 1;
}

static __inline void
x87_emms(void)
{
    uint64_t *p;
    if (fpu_softfloat) {
        fpu_state.tag = 0xffff;
        fpu_state.tos = 0; /* reset FPU Top-Of-Stack */
    } else {
        p  = (uint64_t *) cpu_state.tag;
        *p = 0;
    }
    cpu_state.ismmx = 0;
}

uint16_t x87_gettag(void);
void     x87_settag(uint16_t new_tag);

#define TAG_EMPTY 0
#define TAG_VALID (1 << 0)
/*Hack for FPU copy. If set then MM[].q contains the 64-bit integer loaded by FILD*/
#ifdef USE_NEW_DYNAREC
#    define TAG_UINT64 (1 << 7)
#else
#    define TAG_UINT64 (1 << 2)
#endif

/*Old dynarec stuff.*/
#define TAG_NOT_UINT64       0xfb

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

#define FPU_CW_Reserved_Bits    (0xe0c0)  /* reserved bits */

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

#define FPU_check_pending_exceptions()        \
    do {                                      \
        if (fpu_state.swd & FPU_SW_Summary) { \
            if (cr0 & 0x20)                   \
                new_ne = 1;                   \
            else                              \
                picint(1 << 13);              \
            return 1;                         \
        }                                     \
    } while (0)
