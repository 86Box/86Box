/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel UPI-42/MCS-48 microcontroller emulation.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2022 RichardG.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define fatal printf
#define pclog printf

enum {
    UPI42_8042 = 0,
    UPI42_80C42
};

typedef struct _upi42_ {
    int (*ops[256])(struct _upi42_ *upi42, uint32_t fetchdat);
    uint8_t ram[256], rom[4096], /* memory */
        ports[7],                /* I/O ports */
        dbb_in, dbb_out;         /* UPI-42 data buffer */

    uint8_t rammask,
        a,   /* accumulator */
        t,   /* timer counter */
        psw, /* program status word */
        sts; /* UPI-42 status */

    uint16_t pc; /* program counter */

    unsigned int prescaler : 5, tf : 1, tcnti : 1, run_timer : 1, run_counter : 1, skip_timer_inc : 1, /* timer/counter */
        i : 1, i_asserted : 1, tcnti_asserted : 1, irq_mask : 1,                                       /* interrupts */
        dbf : 1,                                                                                       /* ROM bank */
        t0 : 1, t1 : 1,                                                                                /* T0/T1 signals */
        flags   : 1,                                                                                   /* buffer flag pins */
        suspend : 1;                                                                                   /* 80C42 suspend flag */

    int cycs; /* cycle counter */
} upi42_t;

#define UPI42_REG_READ(upi42, r)      ((upi42->psw & 0x10) ? (upi42->ram[24 + ((r) &7)]) : (upi42->ram[(r) &7]))
#define UPI42_REG_WRITE(upi42, r, op) ((upi42->psw & 0x10) ? (upi42->ram[24 + ((r) &7)] op) : (upi42->ram[(r) &7] op))

static inline void
upi42_mirror_f0(upi42_t *upi42)
{
    /* Update status register F0 flag to match PSW F0 flag. */
    upi42->sts = ((upi42->psw & 0x20) >> 3) | (upi42->sts & ~0x04);
}

static int
upi42_op_MOV_A_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = UPI42_REG_READ(upi42, fetchdat);
    return 1;
}

static int
upi42_op_MOV_Rr_A(upi42_t *upi42, uint32_t fetchdat)
{
    UPI42_REG_WRITE(upi42, fetchdat, = upi42->a);
    return 1;
}

static int
upi42_op_MOV_A_indRr(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = upi42->ram[upi42->ram[fetchdat & 1] & upi42->rammask];
    return 1;
}

static int
upi42_op_MOV_indRr_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->ram[upi42->ram[fetchdat & 1] & upi42->rammask] = upi42->a;
    return 1;
}

static int
upi42_op_MOV_Rr_imm(upi42_t *upi42, uint32_t fetchdat)
{
    UPI42_REG_WRITE(upi42, fetchdat, = fetchdat >> 8);
    upi42->cycs--;
    return 2;
}

static int
upi42_op_MOV_indRr_imm(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->ram[upi42->ram[fetchdat & 1] & upi42->rammask] = fetchdat >> 8;
    upi42->cycs--;
    return 2;
}

static int
upi42_op_MOV_A_imm(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = fetchdat >> 8;
    upi42->cycs--;
    return 2;
}

static int
upi42_op_MOV_A_PSW(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = upi42->psw;
    upi42_mirror_f0(upi42);
    return 1;
}

static int
upi42_op_MOV_PSW_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->psw = upi42->a;
    upi42_mirror_f0(upi42);
    return 1;
}

static int
upi42_op_MOV_A_T(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = upi42->t;
    return 1;
}

static int
upi42_op_MOV_T_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->t = upi42->a;
    return 1;
}

static int
upi42_op_MOV_STS_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->sts = (upi42->a & 0xf0) | (upi42->sts & 0x0f);
    return 1;
}

static int
upi42_op_MOVP_A_indA(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = upi42->rom[(upi42->pc & 0xff00) | upi42->a];
    upi42->cycs--;
    return 1;
}

static int
upi42_op_MOVP3_A_indA(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = upi42->rom[0x300 | upi42->a];
    upi42->cycs--;
    return 1;
}

static int
upi42_op_XCH_A_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    uint8_t temp = upi42->a;
    upi42->a     = UPI42_REG_READ(upi42, fetchdat);
    UPI42_REG_WRITE(upi42, fetchdat, = temp);
    return 1;
}

static int
upi42_op_XCH_A_indRr(upi42_t *upi42, uint32_t fetchdat)
{
    uint8_t temp = upi42->a, addr = upi42->ram[fetchdat & 1] & upi42->rammask;
    upi42->a         = upi42->ram[addr];
    upi42->ram[addr] = temp;
    return 1;
}

static int
upi42_op_XCHD_A_indRr(upi42_t *upi42, uint32_t fetchdat)
{
    uint8_t temp = upi42->a, addr = upi42->ram[fetchdat & 1] & upi42->rammask;
    upi42->a         = (upi42->a & 0xf0) | (upi42->ram[addr] & 0x0f);
    upi42->ram[addr] = (upi42->ram[addr] & 0xf0) | (temp & 0x0f);
    return 1;
}

static int
upi42_op_SWAP_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = (upi42->a << 4) | (upi42->a >> 4);
    return 1;
}

static int
upi42_op_IN_A_Pp(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = upi42->ports[fetchdat & 3];
    upi42->cycs--;
    return 1;
}

static int
upi42_op_IN_A_DBB(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = upi42->dbb_in;
    return 1;
}

static int
upi42_op_OUTL_Pp_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->ports[fetchdat & 3] = upi42->a;
    upi42->cycs--;
    return 1;
}

static int
upi42_op_OUT_DBB_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->dbb_out = upi42->a;
    upi42->sts |= 0x01;
    return 1;
}

static int
upi42_op_MOVD_A_Pp(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = upi42->ports[4 | (fetchdat & 3)] & 0x0f;
    upi42->cycs--;
    return 1;
}

static int
upi42_op_MOVD_Pp_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->ports[4 | (fetchdat & 3)] = upi42->a & 0x0f;
    upi42->cycs--;
    return 1;
}

static int
upi42_op_ANL_A_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a &= UPI42_REG_READ(upi42, fetchdat);
    return 1;
}

static int
upi42_op_ORL_A_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a |= UPI42_REG_READ(upi42, fetchdat);
    return 1;
}

static int
upi42_op_XRL_A_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a ^= UPI42_REG_READ(upi42, fetchdat);
    return 1;
}

static int
upi42_op_ANL_A_indRr(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a &= upi42->ram[upi42->ram[fetchdat & 1] & upi42->rammask];
    return 1;
}

static int
upi42_op_ORL_A_indRr(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a |= upi42->ram[upi42->ram[fetchdat & 1] & upi42->rammask];
    return 1;
}

static int
upi42_op_XRL_A_indRr(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a ^= upi42->ram[upi42->ram[fetchdat & 1] & upi42->rammask];
    return 1;
}

static int
upi42_op_ANL_A_imm(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a &= fetchdat >> 8;
    upi42->cycs--;
    return 2;
}

static int
upi42_op_ORL_A_imm(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a |= fetchdat >> 8;
    upi42->cycs--;
    return 2;
}

static int
upi42_op_XRL_A_imm(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a ^= fetchdat >> 8;
    upi42->cycs--;
    return 2;
}

static int
upi42_op_ANL_Pp_imm(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->ports[fetchdat & 3] &= fetchdat >> 8;
    upi42->cycs--;
    return 2;
}

static int
upi42_op_ORL_Pp_imm(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->ports[fetchdat & 3] |= fetchdat >> 8;
    upi42->cycs--;
    return 2;
}

static int
upi42_op_ANLD_Pp_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->ports[4 | (fetchdat & 3)] &= upi42->a;
    upi42->cycs--;
    return 1;
}

static int
upi42_op_ORLD_Pp_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->ports[4 | (fetchdat & 3)] |= upi42->a;
    upi42->cycs--;
    return 1;
}

static int
upi42_op_RR_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = (upi42->a << 7) | (upi42->a >> 1);
    return 1;
}

static int
upi42_op_RL_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = (upi42->a >> 7) | (upi42->a << 1);
    return 1;
}

static int
upi42_op_RRC_A(upi42_t *upi42, uint32_t fetchdat)
{
    uint8_t temp = upi42->a;
    upi42->a     = (upi42->psw & 0x80) | (temp >> 1);
    upi42->psw   = (temp << 7) | (upi42->psw & ~0x80);
    return 1;
}

static int
upi42_op_RLC_A(upi42_t *upi42, uint32_t fetchdat)
{
    uint8_t temp = upi42->a;
    upi42->a     = (temp << 1) | (upi42->psw >> 7);
    upi42->psw   = (temp & 0x80) | (upi42->psw & ~0x80);
    return 1;
}

static int
upi42_op_INC_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a++;
    return 1;
}

static int
upi42_op_INC_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    UPI42_REG_WRITE(upi42, fetchdat, ++);
    return 1;
}

static int
upi42_op_INC_indRr(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->ram[upi42->ram[fetchdat & 1] & upi42->rammask]++;
    return 1;
}

static int
upi42_op_DEC_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a--;
    return 1;
}

static int
upi42_op_DEC_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    UPI42_REG_WRITE(upi42, fetchdat, --);
    return 1;
}

static int
upi42_op_DJNZ_Rr_imm(upi42_t *upi42, uint32_t fetchdat)
{
    UPI42_REG_WRITE(upi42, fetchdat, --);
    if (UPI42_REG_READ(upi42, fetchdat)) {
        upi42->pc = (upi42->pc & 0xff00) | ((fetchdat >> 8) & 0xff);
        return 0;
    } else {
        return 2;
    }
}

static int
upi42_op_ADD_A_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    int res    = upi42->a + UPI42_REG_READ(upi42, fetchdat);
    upi42->a   = res;
    upi42->psw = ((res >> 1) & 0x80) | (upi42->psw & ~0x80);
    return 1;
}

static int
upi42_op_ADDC_A_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    int res    = upi42->a + (upi42->psw >> 7) + UPI42_REG_READ(upi42, fetchdat);
    upi42->a   = res;
    upi42->psw = ((res >> 1) & 0x80) | (upi42->psw & ~0x80);
    return 1;
}

static int
upi42_op_ADD_A_indRr(upi42_t *upi42, uint32_t fetchdat)
{
    int res    = upi42->a + upi42->ram[UPI42_REG_READ(upi42, fetchdat) & upi42->rammask];
    upi42->a   = res;
    upi42->psw = ((res >> 1) & 0x80) | (upi42->psw & ~0x80);
    return 1;
}

static int
upi42_op_ADDC_A_indRr(upi42_t *upi42, uint32_t fetchdat)
{
    int res    = upi42->a + (upi42->psw >> 7) + upi42->ram[UPI42_REG_READ(upi42, fetchdat) & upi42->rammask];
    upi42->a   = res;
    upi42->psw = ((res >> 1) & 0x80) | (upi42->psw & ~0x80);
    return 1;
}

static int
upi42_op_ADD_A_imm(upi42_t *upi42, uint32_t fetchdat)
{
    int res    = upi42->a + (fetchdat >> 8);
    upi42->a   = res;
    upi42->psw = ((res >> 1) & 0x80) | (upi42->psw & ~0x80);
    upi42->cycs--;
    return 2;
}

static int
upi42_op_ADDC_A_imm(upi42_t *upi42, uint32_t fetchdat)
{
    int res    = upi42->a + (upi42->psw >> 7) + (fetchdat >> 8);
    upi42->a   = res;
    upi42->psw = ((res >> 1) & 0x80) | (upi42->psw & ~0x80);
    upi42->cycs--;
    return 2;
}

static int
upi42_op_CLR_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = 0;
    return 1;
}

static int
upi42_op_CPL_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = ~upi42->a;
    return 1;
}

static int
upi42_op_DA_A(upi42_t *upi42, uint32_t fetchdat)
{
    if (((upi42->a & 0x0f) > 9) || (upi42->psw & 0x40))
        upi42->a += 6;
    if (((upi42->a >> 4) > 9) || (upi42->psw & 0x80)) {
        int res    = upi42->a + (6 << 4);
        upi42->a   = res;
        upi42->psw = ((res >> 1) & 0x80) | (upi42->psw & ~0x80);
    }
    return 1;
}

static int
upi42_op_CLR_C(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->psw &= ~0x80;
    return 1;
}

static int
upi42_op_CPL_C(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->psw ^= 0x80;
    return 1;
}

static int
upi42_op_CLR_F0(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->psw &= ~0x20;
    upi42_mirror_f0(upi42);
    return 1;
}

static int
upi42_op_CPL_F0(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->psw ^= 0x20;
    upi42_mirror_f0(upi42);
    return 1;
}

static int
upi42_op_CLR_F1(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->sts &= ~0x08;
    return 1;
}

static int
upi42_op_CPL_F1(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->sts ^= 0x08;
    return 1;
}

static int
upi42_op_EN_I(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->i              = 1;
    upi42->skip_timer_inc = 1;
    return 1;
}

static int
upi42_op_DIS_I(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->i              = 0;
    upi42->skip_timer_inc = 1;
    return 1;
}

static int
upi42_op_EN_TCNTI(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->tcnti = 1;
    return 1;
}

static int
upi42_op_DIS_TCNTI(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->tcnti = upi42->tcnti_asserted = 0;
    return 1;
}

static int
upi42_op_STRT_T(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->run_timer      = 1;
    upi42->prescaler      = 0;
    upi42->skip_timer_inc = 1;
    return 1;
}

static int
upi42_op_STRT_CNT(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->run_counter    = 1;
    upi42->skip_timer_inc = 1;
    return 1;
}

static int
upi42_op_STOP_TCNT(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->run_timer = upi42->run_counter = 0;
    upi42->skip_timer_inc                 = 1;
    return 1;
}

static int
upi42_op_SEL_PMB0(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->dbf = 0;
    return 1;
}

static int
upi42_op_SEL_PMB1(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->dbf = 1;
    return 1;
}

static int
upi42_op_SEL_RB0(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->psw &= ~0x10;
    return 1;
}

static int
upi42_op_SEL_RB1(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->psw |= 0x10;
    return 1;
}

static int
upi42_op_NOP(upi42_t *upi42, uint32_t fetchdat)
{
    return 1;
}

static int
upi42_op_CALL_imm(upi42_t *upi42, uint32_t fetchdat)
{
    /* Push new frame onto stack. */
    uint8_t sp           = (upi42->psw & 0x07) << 1;
    upi42->ram[8 + sp++] = upi42->pc; /* stack frame format is undocumented! */
    upi42->ram[8 + sp++] = (upi42->psw & 0xf0) | ((upi42->pc >> 8) & 0x07);
    upi42->psw           = (upi42->psw & 0xf8) | (sp >> 1);

    /* Load new program counter. */
    upi42->pc = (upi42->dbf << 11) | ((fetchdat << 3) & 0x0700) | ((fetchdat >> 8) & 0x00ff);

    /* Don't decrease cycle counter if this is an interrupt call. */
    if (fetchdat & 0xff)
        upi42->cycs--;
    return 0;
}

static int
upi42_op_RET(upi42_t *upi42, uint32_t fetchdat)
{
    /* Pop frame off the stack. */
    uint8_t sp     = (upi42->psw & 0x07) << 1;
    uint8_t frame1 = upi42->ram[8 + --sp];
    uint8_t frame0 = upi42->ram[8 + --sp];
    upi42->psw     = (upi42->psw & 0xf8) | (sp >> 1);

    /* Load new program counter. */
    upi42->pc = ((frame1 & 0x0f) << 8) | frame0;

    /* Load new Program Status Word and unmask interrupts if this is RETR. */
    if (fetchdat & 0x10) {
        upi42->psw = (frame1 & 0xf0) | (upi42->psw & 0x0f);
        upi42_mirror_f0(upi42);

        upi42->irq_mask = 0;
    }

    upi42->cycs--;
    return 0;
}

static int
upi42_op_JMP_imm(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->pc = (upi42->dbf << 11) | ((fetchdat << 3) & 0x0700) | ((fetchdat >> 8) & 0x00ff);
    upi42->cycs--;
    return 0;
}

static int
upi42_op_JMPP_indA(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->pc = (upi42->pc & 0xff00) | upi42->a;
    upi42->cycs--;
    return 0;
}

#define UPI42_COND_JMP_IMM(insn, cond, post)                               \
    static int                                                             \
        upi42_op_##insn##_imm(upi42_t *upi42, uint32_t fetchdat)           \
    {                                                                      \
        if (cond)                                                          \
            upi42->pc = (upi42->pc & 0xff00) | ((fetchdat >> 8) & 0x00ff); \
        post                                                               \
            upi42->cycs--;                                                 \
        return 2 * !(cond);                                                \
    }
UPI42_COND_JMP_IMM(JC, upi42->psw & 0x80, ;)
UPI42_COND_JMP_IMM(JNC, !(upi42->psw & 0x80), ;)
UPI42_COND_JMP_IMM(JZ, !upi42->a, ;)
UPI42_COND_JMP_IMM(JNZ, upi42->a, ;)
UPI42_COND_JMP_IMM(JT0, upi42->t0, ;)
UPI42_COND_JMP_IMM(JNT0, !upi42->t0, ;)
UPI42_COND_JMP_IMM(JT1, upi42->t1, ;)
UPI42_COND_JMP_IMM(JNT1, !upi42->t1, ;)
UPI42_COND_JMP_IMM(JF0, upi42->psw & 0x20, ;)
UPI42_COND_JMP_IMM(JF1, upi42->sts & 0x08, ;)
UPI42_COND_JMP_IMM(JTF, !upi42->tf, upi42->tf = 0;)
UPI42_COND_JMP_IMM(JBb, upi42->a &(1 << ((fetchdat >> 5) & 7)), ;)
UPI42_COND_JMP_IMM(JNIBF, !(upi42->sts & 0x02), ;)
UPI42_COND_JMP_IMM(JOBF, upi42->sts & 0x01, ;)

static int
upi42_op_EN_A20(upi42_t *upi42, uint32_t fetchdat)
{
    /* Enable fast A20 until reset. */
    return 1;
}

static int
upi42_op_EN_DMA(upi42_t *upi42, uint32_t fetchdat)
{
    return 1;
}

static int
upi42_op_EN_FLAGS(upi42_t *upi42, uint32_t fetchdat)
{
    return 1;
}

static int
upi42_op_SUSPEND(upi42_t *upi42, uint32_t fetchdatr)
{
    /* Inhibit execution until reset. */
    upi42->suspend = 1;
    return 1;
}

static void
upi42_exec(void *priv)
{
    upi42_t *upi42 = (upi42_t *) priv;

    /* Skip interrupt handling and code execution if we're suspended or in a multi-cycle instruction. */
    if (upi42->suspend || ++upi42->cycs < 0)
        return;

    /* Trigger interrupt if requested. */
    if (upi42->irq_mask) {
        /* Masked, we're currently in an ISR. */
    } else if (upi42->i_asserted) {
        /* External interrupt. Higher priority than the timer interrupt. */
        upi42->irq_mask   = 1;
        upi42->i_asserted = 0;
        upi42_op_CALL_imm(upi42, 3 << 8);
        return;
    } else if (upi42->tcnti_asserted) {
        /* Timer interrupt. */
        upi42->irq_mask       = 1;
        upi42->tcnti_asserted = 0;
        upi42_op_CALL_imm(upi42, 7 << 8);
        return;
    }

    /* Fetch instruction. */
    uint32_t fetchdat = *((uint32_t *) &upi42->rom[upi42->pc]);
    pclog("%04X @ %04X   R0=%02X", fetchdat & 0xffff, upi42->pc, upi42->ram[0]);

    /* Decode instruction. */
    uint8_t insn = fetchdat & 0xff;
    if (upi42->ops[insn]) {
        /* Execute instruction and increment program counter. */
        upi42->pc += upi42->ops[insn](upi42, fetchdat);

        /* Decrement cycle counter. Multi-cycle instructions also decrement within their code. */
        upi42->cycs--;
    } else {
        fatal("UPI42: Unknown opcode %02X (%08X)\n", insn, fetchdat);
        return;
    }

    /* Some instructions don't increment the timer. */
    if (upi42->skip_timer_inc) {
        upi42->skip_timer_inc = 0;
    } else {
        /* Increment counter once the prescaler overflows,
           and set timer flag once the main value overflows. */
        if ((++upi42->prescaler == 0) && (++upi42->t == 0)) {
            upi42->tf = 1;

            /* Fire counter interrupt if enabled. */
            if (upi42->tcnti)
                upi42->tcnti_asserted = 1;
        }
    }
}

static const int (*ops_80c42[256])(upi42_t *upi42, uint32_t fetchdat) = {
    // clang-format off
             /* 0 / 8 */            /* 1 / 9 */            /* 2 / a */            /* 3 / b */            /* 4 / c */            /* 5 / d */            /* 6 / e */            /* 7 / f */
    /* 00 */ upi42_op_NOP,          NULL,                  upi42_op_OUT_DBB_A,    upi42_op_ADD_A_imm,    upi42_op_JMP_imm,      upi42_op_EN_I,         NULL,                  upi42_op_DEC_A,
    /* 08 */ upi42_op_IN_A_Pp,      upi42_op_IN_A_Pp,      upi42_op_IN_A_Pp,      NULL,                  upi42_op_MOVD_A_Pp,    upi42_op_MOVD_A_Pp,    upi42_op_MOVD_A_Pp,    upi42_op_MOVD_A_Pp,
    /* 10 */ upi42_op_INC_indRr,    upi42_op_INC_indRr,    upi42_op_JBb_imm,      upi42_op_ADDC_A_imm,   upi42_op_CALL_imm,     upi42_op_DIS_I,        upi42_op_JTF_imm,      upi42_op_INC_A,
    /* 18 */ upi42_op_INC_Rr,       upi42_op_INC_Rr,       upi42_op_INC_Rr,       upi42_op_INC_Rr,       upi42_op_INC_Rr,       upi42_op_INC_Rr,       upi42_op_INC_Rr,       upi42_op_INC_Rr,
    /* 20 */ upi42_op_XCH_A_indRr,  upi42_op_XCH_A_indRr,  upi42_op_IN_A_DBB,     upi42_op_MOV_A_imm,    upi42_op_JMP_imm,      upi42_op_EN_TCNTI,     upi42_op_JNT0_imm,     upi42_op_CLR_A,
    /* 28 */ upi42_op_XCH_A_Rr,     upi42_op_XCH_A_Rr,     upi42_op_XCH_A_Rr,     upi42_op_XCH_A_Rr,     upi42_op_XCH_A_Rr,     upi42_op_XCH_A_Rr,     upi42_op_XCH_A_Rr,     upi42_op_XCH_A_Rr,
    /* 30 */ upi42_op_XCHD_A_indRr, upi42_op_XCHD_A_indRr, upi42_op_JBb_imm,      upi42_op_EN_A20,       upi42_op_CALL_imm,     upi42_op_DIS_TCNTI,    upi42_op_JT0_imm,      upi42_op_CPL_A,
    /* 38 */ upi42_op_OUTL_Pp_A,    upi42_op_OUTL_Pp_A,    upi42_op_OUTL_Pp_A,    upi42_op_OUTL_Pp_A,    upi42_op_MOVD_Pp_A,    upi42_op_MOVD_Pp_A,    upi42_op_MOVD_Pp_A,    upi42_op_MOVD_Pp_A,
    /* 40 */ upi42_op_ORL_A_indRr,  upi42_op_ORL_A_indRr,  upi42_op_MOV_A_T,      upi42_op_ORL_A_imm,    upi42_op_JMP_imm,      upi42_op_STRT_CNT,     upi42_op_JNT1_imm,     upi42_op_SWAP_A,
    /* 48 */ upi42_op_ORL_A_Rr,     upi42_op_ORL_A_Rr,     upi42_op_ORL_A_Rr,     upi42_op_ORL_A_Rr,     upi42_op_ORL_A_Rr,     upi42_op_ORL_A_Rr,     upi42_op_ORL_A_Rr,     upi42_op_ORL_A_Rr,
    /* 50 */ upi42_op_ANL_A_indRr,  upi42_op_ANL_A_indRr,  upi42_op_JBb_imm,      upi42_op_ANL_A_imm,    upi42_op_CALL_imm,     upi42_op_STRT_T,       upi42_op_JT1_imm,      upi42_op_DA_A,
    /* 58 */ upi42_op_ANL_A_Rr,     upi42_op_ANL_A_Rr,     upi42_op_ANL_A_Rr,     upi42_op_ANL_A_Rr,     upi42_op_ANL_A_Rr,     upi42_op_ANL_A_Rr,     upi42_op_ANL_A_Rr,     upi42_op_ANL_A_Rr,
    /* 60 */ upi42_op_ADD_A_indRr,  upi42_op_ADD_A_indRr,  upi42_op_MOV_T_A,      NULL,                  upi42_op_JMP_imm,      upi42_op_STOP_TCNT,    NULL,                  upi42_op_RRC_A,
    /* 68 */ upi42_op_ADD_A_Rr,     upi42_op_ADD_A_Rr,     upi42_op_ADD_A_Rr,     upi42_op_ADD_A_Rr,     upi42_op_ADD_A_Rr,     upi42_op_ADD_A_Rr,     upi42_op_ADD_A_Rr,     upi42_op_ADD_A_Rr,
    /* 70 */ upi42_op_ADDC_A_indRr, upi42_op_ADDC_A_indRr, upi42_op_JBb_imm,      NULL,                  upi42_op_CALL_imm,     NULL,                  upi42_op_JF1_imm,      upi42_op_RR_A,
    /* 78 */ upi42_op_ADDC_A_Rr,    upi42_op_ADDC_A_Rr,    upi42_op_ADDC_A_Rr,    upi42_op_ADDC_A_Rr,    upi42_op_ADDC_A_Rr,    upi42_op_ADDC_A_Rr,    upi42_op_ADDC_A_Rr,    upi42_op_ADDC_A_Rr,
    /* 80 */ NULL,                  NULL,                  upi42_op_SUSPEND,      upi42_op_RET,          upi42_op_JMP_imm,      upi42_op_CLR_F0,       upi42_op_JOBF_imm,     NULL,
    /* 88 */ upi42_op_ORL_Pp_imm,   upi42_op_ORL_Pp_imm,   upi42_op_ORL_Pp_imm,   upi42_op_ORL_Pp_imm,   upi42_op_ORLD_Pp_A,    upi42_op_ORLD_Pp_A,    upi42_op_ORLD_Pp_A,    upi42_op_ORLD_Pp_A,
    /* 90 */ upi42_op_MOV_STS_A,    NULL,                  upi42_op_JBb_imm,      upi42_op_RET,          upi42_op_CALL_imm,     upi42_op_CPL_F0,       upi42_op_JNZ_imm,      upi42_op_CLR_C,
    /* 98 */ upi42_op_ANL_Pp_imm,   upi42_op_ANL_Pp_imm,   upi42_op_ANL_Pp_imm,   upi42_op_ANL_Pp_imm,   upi42_op_ANLD_Pp_A,    upi42_op_ANLD_Pp_A,    upi42_op_ANLD_Pp_A,    upi42_op_ANLD_Pp_A,
    /* a0 */ upi42_op_MOV_indRr_A,  upi42_op_MOV_indRr_A,  NULL,                  upi42_op_MOVP_A_indA,  upi42_op_JMP_imm,      upi42_op_CLR_F1,       NULL,                  upi42_op_CPL_C,
    /* a8 */ upi42_op_MOV_Rr_A,     upi42_op_MOV_Rr_A,     upi42_op_MOV_Rr_A,     upi42_op_MOV_Rr_A,     upi42_op_MOV_Rr_A,     upi42_op_MOV_Rr_A,     upi42_op_MOV_Rr_A,     upi42_op_MOV_Rr_A,
    /* b0 */ upi42_op_MOV_indRr_imm,upi42_op_MOV_indRr_imm,upi42_op_JBb_imm,      upi42_op_JMPP_indA,    upi42_op_CALL_imm,     upi42_op_CPL_F1,       upi42_op_JF0_imm,      NULL,
    /* b8 */ upi42_op_MOV_Rr_imm,   upi42_op_MOV_Rr_imm,   upi42_op_MOV_Rr_imm,   upi42_op_MOV_Rr_imm,   upi42_op_MOV_Rr_imm,   upi42_op_MOV_Rr_imm,   upi42_op_MOV_Rr_imm,   upi42_op_MOV_Rr_imm,
    /* c0 */ NULL,                  NULL,                  NULL,                  NULL,                  upi42_op_JMP_imm,      NULL,                  upi42_op_JZ_imm,       upi42_op_MOV_A_PSW,
    /* c8 */ upi42_op_DEC_Rr,       upi42_op_DEC_Rr,       upi42_op_DEC_Rr,       upi42_op_DEC_Rr,       upi42_op_DEC_Rr,       upi42_op_DEC_Rr,       upi42_op_DEC_Rr,       upi42_op_DEC_Rr,
    /* d0 */ upi42_op_XRL_A_indRr,  upi42_op_XRL_A_indRr,  upi42_op_JBb_imm,      upi42_op_XRL_A_imm,    upi42_op_CALL_imm,     NULL,                  upi42_op_JNIBF_imm,    upi42_op_MOV_PSW_A,
    /* d8 */ upi42_op_XRL_A_Rr,     upi42_op_XRL_A_Rr,     upi42_op_XRL_A_Rr,     upi42_op_XRL_A_Rr,     upi42_op_XRL_A_Rr,     upi42_op_XRL_A_Rr,     upi42_op_XRL_A_Rr,     upi42_op_XRL_A_Rr,
    /* e0 */ NULL,                  NULL,                  upi42_op_SUSPEND,      upi42_op_MOVP3_A_indA, upi42_op_JMP_imm,      upi42_op_EN_DMA,       upi42_op_JNC_imm,      upi42_op_RL_A,
    /* e8 */ upi42_op_DJNZ_Rr_imm,  upi42_op_DJNZ_Rr_imm,  upi42_op_DJNZ_Rr_imm,  upi42_op_DJNZ_Rr_imm,  upi42_op_DJNZ_Rr_imm,  upi42_op_DJNZ_Rr_imm,  upi42_op_DJNZ_Rr_imm,  upi42_op_DJNZ_Rr_imm,
    /* f0 */ upi42_op_MOV_A_indRr,  upi42_op_MOV_A_indRr,  upi42_op_JBb_imm,      NULL,                  upi42_op_CALL_imm,     upi42_op_EN_FLAGS,     upi42_op_JC_imm,       upi42_op_RLC_A,
    /* f8 */ upi42_op_MOV_A_Rr,     upi42_op_MOV_A_Rr,     upi42_op_MOV_A_Rr,     upi42_op_MOV_A_Rr,     upi42_op_MOV_A_Rr,     upi42_op_MOV_A_Rr,     upi42_op_MOV_A_Rr,     upi42_op_MOV_A_Rr
    // clang-format on
};

static void
upi42_reset(upi42_t *upi42)
{
    upi42->pc  = 0;              /* program counter */
    upi42->psw = 0;              /* stack pointer, register bank and F0 */
    upi42->dbf = 0;              /* memory bank */
    upi42->i = upi42->tcnti = 0; /* both interrupts */
    upi42->tf               = 0; /* timer flag */
    upi42->sts              = 0; /* F1 */
    upi42->suspend          = 0; /* 80C42 suspend flag */
}

static upi42_t *
upi42_init(int type)
{
    /* Allocate state structure. */
    upi42_t *upi42 = (upi42_t *) malloc(sizeof(upi42_t));
    memset(upi42, 0, sizeof(upi42_t));

    /* Build instruction table. */
    memcpy(upi42->ops, ops_80c42, sizeof(ops_80c42));
    if (type < UPI42_80C42) {
        /* Remove 80C42-only instructions. */
        upi42->ops[0x33] = NULL; /* EN A20 */
        upi42->ops[0x63] = NULL; /* SEL PMB0 */
        upi42->ops[0x73] = NULL; /* SEL PMB1 */
        upi42->ops[0x42] = NULL; /* SUSPEND */
        upi42->ops[0xe2] = NULL; /* SUSPEND */
    }

    return upi42;
}

int
main(int argc, char **argv)
{
    upi42_t *upi42 = upi42_init(UPI42_8042);

    /* Load ROM. */
    FILE *f = fopen("1503033.bin", "rb");
    fread(upi42->rom, 1, sizeof(upi42->rom), f);
    fclose(f);

    /* Start execution. */
    char buf[256];
    while (1) {
        upi42->sts |= 0x02;
        upi42->sts |= 0x08;
        upi42->dbb_in = 0xaa;
        upi42->cycs   = 0;

        upi42_exec(upi42);
        fgets(buf, 256, stdin);
    }
}
