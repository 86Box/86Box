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

#ifdef UPI42_STANDALONE
#    define fatal(...)              \
        {                           \
            upi42_log(__VA_ARGS__); \
            abort();                \
        }
#    define upi42_log(...)       \
        {                        \
            printf(__VA_ARGS__); \
            fflush(stdout);      \
        }
#else
#    include <stdarg.h>
#    define HAVE_STDARG_H
#    include <86box/86box.h>
#    include <86box/device.h>
#    include <86box/io.h>
#    include <86box/timer.h>

#    ifdef ENABLE_UPI42_LOG
int upi42_do_log = ENABLE_UPI42_LOG;

void
upi42_log(const char *fmt, ...)
{
    va_list ap;

    if (upi42_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#    else
#        define upi42_log(fmt, ...)
#    endif
#endif

#define UPI42_REG(upi42, r, op) ((upi42->psw & 0x10) ? (upi42->ram[24 + ((r) &7)] op) : (upi42->ram[(r) &7] op))

#define UPI42_ROM_SHIFT         0  /* actually the mask */
#define UPI42_RAM_SHIFT         16 /* actually the mask */
#define UPI42_TYPE_MCS          (0 << 24)
#define UPI42_TYPE_UPI          (1 << 24)
#define UPI42_EXT_C42           (1 << 25)

#define UPI42_8048              ((1023 << UPI42_ROM_SHIFT) | (63 << UPI42_RAM_SHIFT) | UPI42_TYPE_MCS)
#define UPI42_8049              ((2047 << UPI42_ROM_SHIFT) | (127 << UPI42_RAM_SHIFT) | UPI42_TYPE_MCS)
#define UPI42_8041              ((1023 << UPI42_ROM_SHIFT) | (127 << UPI42_RAM_SHIFT) | UPI42_TYPE_UPI)
#define UPI42_8042              ((2047 << UPI42_ROM_SHIFT) | (255 << UPI42_RAM_SHIFT) | UPI42_TYPE_UPI)
#define UPI42_80C42             ((4095 << UPI42_ROM_SHIFT) | (255 << UPI42_RAM_SHIFT) | UPI42_TYPE_UPI | UPI42_EXT_C42)

typedef struct _upi42_ {
    int (*ops[256])(struct _upi42_ *upi42, uint32_t fetchdat);
    uint32_t type;
    uint8_t  ram[256], *rom,       /* memory */
        ports_in[8], ports_out[8], /* I/O ports */
        dbb_in, dbb_out;           /* UPI-42 data buffer */

    uint8_t rammask, /* RAM mask */
        a,           /* accumulator */
        t,           /* timer counter */
        psw,         /* program status word */
        sts;         /* UPI-42 status */

    uint16_t pc, rommask; /* program counter and ROM mask */

    unsigned int prescaler : 5, tf : 1, skip_timer_inc : 1, /* timer/counter */
        run_timer : 1, run_counter : 1, tcnti : 1,          /* timer/counter enables */
        i : 1, i_raise : 1, tcnti_raise : 1, irq_mask : 1,  /* interrupts */
        t0 : 1, t1 : 1,                                     /* T0/T1 signals */
        flags : 1, dbf : 1, suspend : 1;                    /* UPI-42 flags */

    int cycs; /* cycle counter */

#ifndef UPI42_STANDALONE
    uint8_t  ram_index;
    uint16_t rom_index;
#endif
} upi42_t;

static inline void
upi42_mirror_f0(upi42_t *upi42)
{
    /* Update status register F0 flag to match PSW F0 flag. */
    upi42->sts = ((upi42->psw & 0x20) >> 3) | (upi42->sts & ~0x04);
}

static int
upi42_op_MOV_A_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = UPI42_REG(upi42, fetchdat, );
    return 1;
}

static int
upi42_op_MOV_Rr_A(upi42_t *upi42, uint32_t fetchdat)
{
    UPI42_REG(upi42, fetchdat, = upi42->a);
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
    UPI42_REG(upi42, fetchdat, = fetchdat >> 8);
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
    upi42->a     = UPI42_REG(upi42, fetchdat, );
    UPI42_REG(upi42, fetchdat, = temp);
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
    int port = fetchdat & 3;
    upi42->a = upi42->ports_in[port] & upi42->ports_out[port];
    upi42->cycs--;
    return 1;
}

static int
upi42_op_IN_A_DBB(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a = upi42->dbb_in;
    upi42->sts &= ~0x02; /* clear IBF */
    return 1;
}

static int
upi42_op_OUTL_Pp_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->ports_out[fetchdat & 3] = upi42->a;
    upi42->cycs--;
    return 1;
}

static int
upi42_op_OUT_DBB_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->dbb_out = upi42->a;
    upi42->sts |= 0x01; /* set OBF */
    return 1;
}

static int
upi42_op_MOVD_A_Pp(upi42_t *upi42, uint32_t fetchdat)
{
    int port = 4 | (fetchdat & 3);
    upi42->a = (upi42->ports_in[port] & upi42->ports_out[port]) & 0x0f;
    upi42->cycs--;
    return 1;
}

static int
upi42_op_MOVD_Pp_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->ports_out[4 | (fetchdat & 3)] = upi42->a & 0x0f;
    upi42->cycs--;
    return 1;
}

static int
upi42_op_ANL_A_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a &= UPI42_REG(upi42, fetchdat, );
    return 1;
}

static int
upi42_op_ORL_A_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a |= UPI42_REG(upi42, fetchdat, );
    return 1;
}

static int
upi42_op_XRL_A_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->a ^= UPI42_REG(upi42, fetchdat, );
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
    upi42->ports_out[fetchdat & 3] &= fetchdat >> 8;
    upi42->cycs--;
    return 2;
}

static int
upi42_op_ORL_Pp_imm(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->ports_out[fetchdat & 3] |= fetchdat >> 8;
    upi42->cycs--;
    return 2;
}

static int
upi42_op_ANLD_Pp_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->ports_out[4 | (fetchdat & 3)] &= upi42->a;
    upi42->cycs--;
    return 1;
}

static int
upi42_op_ORLD_Pp_A(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->ports_out[4 | (fetchdat & 3)] |= upi42->a;
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
    UPI42_REG(upi42, fetchdat, ++);
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
    UPI42_REG(upi42, fetchdat, --);
    return 1;
}

static int
upi42_op_DJNZ_Rr_imm(upi42_t *upi42, uint32_t fetchdat)
{
    upi42->cycs--;
    UPI42_REG(upi42, fetchdat, --);
    if (UPI42_REG(upi42, fetchdat, )) {
        upi42->pc = (upi42->pc & 0xff00) | ((fetchdat >> 8) & 0xff);
        return 0;
    } else {
        return 2;
    }
}

static int
upi42_op_ADD_A_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    int res    = upi42->a + UPI42_REG(upi42, fetchdat, );
    upi42->a   = res;
    upi42->psw = ((res >> 1) & 0x80) | (upi42->psw & ~0x80);
    return 1;
}

static int
upi42_op_ADDC_A_Rr(upi42_t *upi42, uint32_t fetchdat)
{
    int res    = upi42->a + (upi42->psw >> 7) + UPI42_REG(upi42, fetchdat, );
    upi42->a   = res;
    upi42->psw = ((res >> 1) & 0x80) | (upi42->psw & ~0x80);
    return 1;
}

static int
upi42_op_ADD_A_indRr(upi42_t *upi42, uint32_t fetchdat)
{
    int res    = upi42->a + upi42->ram[UPI42_REG(upi42, fetchdat, ) & upi42->rammask];
    upi42->a   = res;
    upi42->psw = ((res >> 1) & 0x80) | (upi42->psw & ~0x80);
    return 1;
}

static int
upi42_op_ADDC_A_indRr(upi42_t *upi42, uint32_t fetchdat)
{
    int res    = upi42->a + (upi42->psw >> 7) + upi42->ram[UPI42_REG(upi42, fetchdat, ) & upi42->rammask];
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
    upi42->tcnti = upi42->tcnti_raise = 0;
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
    upi42->ram[8 + sp++] = upi42->pc + 2; /* stack frame format is undocumented! */
    upi42->ram[8 + sp++] = (upi42->psw & 0xf0) | ((upi42->pc >> 8) & 0x07);
    upi42->psw           = (upi42->psw & 0xf8) | (sp >> 1);

    /* Load new program counter. */
    upi42->pc = (upi42->dbf << 11) | ((fetchdat << 3) & 0x0700) | ((fetchdat >> 8) & 0x00ff);

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
        post;                                                              \
        upi42->cycs--;                                                     \
        return 2 * !(cond);                                                \
    }
UPI42_COND_JMP_IMM(JC, upi42->psw & 0x80, )
UPI42_COND_JMP_IMM(JNC, !(upi42->psw & 0x80), )
UPI42_COND_JMP_IMM(JZ, !upi42->a, )
UPI42_COND_JMP_IMM(JNZ, upi42->a, )
UPI42_COND_JMP_IMM(JT0, upi42->t0, )
UPI42_COND_JMP_IMM(JNT0, !upi42->t0, )
UPI42_COND_JMP_IMM(JT1, upi42->t1, )
UPI42_COND_JMP_IMM(JNT1, !upi42->t1, )
UPI42_COND_JMP_IMM(JF0, upi42->psw & 0x20, )
UPI42_COND_JMP_IMM(JF1, upi42->sts & 0x08, )
UPI42_COND_JMP_IMM(JTF, !upi42->tf, upi42->tf = 0)
UPI42_COND_JMP_IMM(JBb, upi42->a &(1 << ((fetchdat >> 5) & 7)), )
UPI42_COND_JMP_IMM(JNIBF, !(upi42->sts & 0x02), )
UPI42_COND_JMP_IMM(JOBF, upi42->sts & 0x01, )

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
    upi42->flags = 1;
    return 1;
}

static int
upi42_op_SUSPEND(upi42_t *upi42, uint32_t fetchdatr)
{
    /* Inhibit execution until reset. */
    upi42->suspend = 1;
    return 1;
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
    /* 60 */ upi42_op_ADD_A_indRr,  upi42_op_ADD_A_indRr,  upi42_op_MOV_T_A,      upi42_op_SEL_PMB0,     upi42_op_JMP_imm,      upi42_op_STOP_TCNT,    NULL,                  upi42_op_RRC_A,
    /* 68 */ upi42_op_ADD_A_Rr,     upi42_op_ADD_A_Rr,     upi42_op_ADD_A_Rr,     upi42_op_ADD_A_Rr,     upi42_op_ADD_A_Rr,     upi42_op_ADD_A_Rr,     upi42_op_ADD_A_Rr,     upi42_op_ADD_A_Rr,
    /* 70 */ upi42_op_ADDC_A_indRr, upi42_op_ADDC_A_indRr, upi42_op_JBb_imm,      upi42_op_SEL_PMB1,     upi42_op_CALL_imm,     NULL,                  upi42_op_JF1_imm,      upi42_op_RR_A,
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
upi42_exec(void *priv)
{
    upi42_t *upi42 = (upi42_t *) priv;

    /* Skip everything if we're suspended, or just process timer if we're in a multi-cycle instruction. */
    if (upi42->suspend)
        return;
    else if (++upi42->cycs < 0)
        goto timer;

    /* Trigger interrupt if requested. */
    if (upi42->irq_mask) {
        /* Masked, we're currently in an ISR. */
    } else if (upi42->i_raise) {
        /* External interrupt. Higher priority than the timer interrupt. */
        upi42->irq_mask = 1;
        upi42->i_raise  = 0;

        upi42->pc -= 2;
        upi42->cycs++;
        upi42_op_CALL_imm(upi42, 3 << 8);
        return;
    } else if (upi42->tcnti_raise) {
        /* Timer interrupt. */
        upi42->irq_mask    = 1;
        upi42->tcnti_raise = 0;

        upi42->pc -= 2;
        upi42->cycs++;
        upi42_op_CALL_imm(upi42, 7 << 8);
        return;
    }

    /* Fetch instruction. */
    uint32_t fetchdat = *((uint32_t *) &upi42->rom[upi42->pc]);

    /* Decode instruction. */
    uint8_t insn = fetchdat & 0xff;
    if (upi42->ops[insn]) {
        /* Execute instruction. */
        int pc_inc = upi42->ops[insn](upi42, fetchdat);

        /* Increment lower 11 bits of the program counter. */
        upi42->pc = (upi42->pc & 0xf800) | ((upi42->pc + pc_inc) & 0x07ff);

        /* Decrement cycle counter. Multi-cycle instructions also decrement within their code. */
        upi42->cycs--;
    } else {
        fatal("UPI42: Unknown opcode %02X (%08X)\n", insn, fetchdat);
        return;
    }

timer:
    /* Process timer. */
    if (!upi42->run_timer) {
        /* Timer disabled. */
    } else if (upi42->skip_timer_inc) {
        /* Some instructions don't increment the timer. */
        upi42->skip_timer_inc = 0;
    } else {
        /* Increment counter once the prescaler overflows,
           and set timer flag once the main value overflows. */
        if ((++upi42->prescaler == 0) && (++upi42->t == 0)) {
            upi42->tf = 1;

            /* Fire counter interrupt if enabled. */
            if (upi42->tcnti)
                upi42->tcnti_raise = 1;
        }
    }
}

uint8_t
upi42_port_read(void *priv, int port)
{
    upi42_t *upi42 = (upi42_t *) priv;

    /* Read base port value. */
    port &= 7;
    uint8_t ret = upi42->ports_in[port] & upi42->ports_out[port];

    /* Apply special meanings. */
    switch (port) {
    }

    upi42_log("UPI42: port_read(%d) = %02X\n", port, ret);
    return ret;
}

void
upi42_port_write(void *priv, int port, uint8_t val)
{
    upi42_t *upi42 = (upi42_t *) priv;

    port &= 7;
    upi42_log("UPI42: port_write(%d, %02X)\n", port, val);

    /* Set input level. */
    upi42->ports_in[port] = val;
}

/* NOTE: The dbb/sts/cmd functions use I/O handler signatures; port is ignored. */

uint8_t
upi42_dbb_read(uint16_t port, void *priv)
{
    upi42_t *upi42 = (upi42_t *) priv;

    uint8_t ret = upi42->dbb_out;
    upi42_log("UPI42: dbb_read(%04X) = %02X\n", port, ret);
    upi42->sts &= ~0x01; /* clear OBF */
    return ret;
}

void
upi42_dbb_write(uint16_t port, uint8_t val, void *priv)
{
    upi42_t *upi42 = (upi42_t *) priv;

    upi42_log("UPI42: dbb_write(%04X, %02X)\n", port, val);
    upi42->dbb_in = val;
    upi42->sts    = (upi42->sts & ~0x08) | 0x02; /* clear F1 and set IBF */
    if (upi42->i)                                /* fire IBF interrupt if enabled */
        upi42->i_raise = 1;
}

uint8_t
upi42_sts_read(uint16_t port, void *priv)
{
    upi42_t *upi42 = (upi42_t *) priv;

    uint8_t ret = upi42->sts;
    upi42_log("UPI42: sts_read(%04X) = %02X\n", port, ret);
    return ret;
}

void
upi42_cmd_write(uint16_t port, uint8_t val, void *priv)
{
    upi42_t *upi42 = (upi42_t *) priv;

    upi42_log("UPI42: cmd_write(%04X, %02X)\n", port, val);
    upi42->dbb_in = val;
    upi42->sts |= 0x0a; /* set F1 and IBF */
    if (upi42->i)       /* fire IBF interrupt if enabled */
        upi42->i_raise = 1;
}

void
upi42_reset(upi42_t *upi42)
{
    upi42->pc      = 0; /* program counter */
    upi42->psw     = 0; /* stack pointer, register bank and F0 */
    upi42->dbf     = 0; /* ROM bank */
    upi42->i       = 0; /* external interrupt */
    upi42->tcnti   = 0; /* timer/counter interrupt */
    upi42->tf      = 0; /* timer flag */
    upi42->sts     = 0; /* F1 */
    upi42->flags   = 0; /* UPI-42 buffer interrupts */
    upi42->suspend = 0; /* 80C42 suspend flag */
}

void
upi42_do_init(upi32_t type, uint8_t *rom)
{
    memset(upi42, 0, sizeof(upi42_t));
    upi42->rom = rom;

    /* Set chip type. */
    upi42->type    = type;
    upi42->rommask = type >> UPI42_ROM_SHIFT;
    upi42->rammask = type >> UPI42_RAM_SHIFT;

    /* Build instruction table. */
    memcpy(upi42->ops, ops_80c42, sizeof(ops_80c42));
    if (!(type & UPI42_EXT_C42)) {
        /* Remove 80C42-only instructions. */
        upi42->ops[0x33] = NULL; /* EN A20 */
        upi42->ops[0x63] = NULL; /* SEL PMB0 */
        upi42->ops[0x73] = NULL; /* SEL PMB1 */
        upi42->ops[0x42] = NULL; /* SUSPEND */
        upi42->ops[0xe2] = NULL; /* SUSPEND */
    }

    memset(upi42_t->ports_in, 0xff, 0x08);
    upi42_t->t0 = 1;
    upi42_t->t1 = 1;
}

void *
upi42_init(uint32_t type, uint8_t *rom)
{
    /* Allocate state structure. */
    upi42_t *upi42 = (upi42_t *) malloc(sizeof(upi42_t));
    upi42_do_init(type, rom);

    return upi42;
}

#ifdef UPI42_STANDALONE
static const char *flags_8042[] = { "OBF", "IBF", "F0", "F1", "ST4", "ST5", "ST6", "ST7" };

int
main(int argc, char **argv)
{
    /* Check arguments. */
    if (argc < 2) {
        upi42_log("Specify a ROM file to execute.\n");
        return 1;
    }

    /* Load ROM. */
    uint8_t rom[4096] = { 0 };
    FILE   *f         = fopen(argv[1], "rb");
    if (!f) {
        upi42_log("Could not read ROM file.\n");
        return 2;
    }
    size_t rom_size = fread(rom, sizeof(rom[0]), sizeof(rom), f);
    fclose(f);

    /* Determine chip type from ROM. */
    upi42_log("%d-byte ROM, ", rom_size);
    uint32_t type;
    switch (rom_size) {
        case 0 ... 1024:
            upi42_log("emulating 8041");
            type = UPI42_8041;
            break;

        case 1025 ... 2048:
            upi42_log("emulating 8042");
            type = UPI42_8042;
            break;

        case 2049 ... 4096:
            upi42_log("emulating 80C42");
            type = UPI42_80C42;
            break;

        default:
            upi42_log("unknown!\n");
            return 3;
    }
    upi42_log(".\n");

    /* Initialize emulator. */
    upi42_t *upi42 = (upi42_t *) upi42_init(type, rom);

    /* Start execution. */
    char cmd, cmd_buf[256];
    int  val, go_until = -1;
    while (1) {
        /* Output status. */
        upi42_log("PC=%04X I=%02X(%02X)  A=%02X", upi42->pc, upi42->rom[upi42->pc], upi42->rom[upi42->pc + 1], upi42->a);
        for (val = 0; val < 8; val++)
            upi42_log(" R%d=%02X", val, UPI42_REG(upi42, val, ));
        upi42_log("  T=%02X PSW=%02X TF=%d I=%d TCNTI=%d", upi42->t, upi42->psw, upi42->tf, upi42->i, upi42->tcnti);
        if (type & UPI42_TYPE_UPI) {
            upi42_log("  STS=%02X", upi42->sts);
            for (val = 0; val < 8; val++) {
                if (upi42->sts & (1 << val)) {
                    upi42_log(" [%s]", flags_8042[val]);
                } else {
                    upi42_log("  %s ", flags_8042[val]);
                }
            }
        }
        upi42_log("\n");

        /* Break for command only if stepping. */
        if ((go_until < 0) || (upi42->pc == go_until)) {
retry:
            go_until = -1;
            upi42_log("> ");

            /* Read command. */
            cmd = '\0';
            scanf("%c", &cmd);

            /* Execute command. */
            switch (cmd) {
                case 'c': /* write command */
                    if (scanf("%X%*c", &val, &cmd_buf))
                        upi42_cmd_write(0, val, upi42);
                    goto retry;

                case 'd': /* write data */
                    if (scanf("%X%*c", &val, &cmd_buf))
                        upi42_dbb_write(0, val, upi42);
                    goto retry;

                case 'g': /* go until */
                    if (!scanf("%X%*c", &go_until, &cmd_buf))
                        go_until = -1;
                    break;

                case 'r':                     /* read data */
                    upi42_dbb_read(0, upi42); /* return value will be logged */
                    goto skip_and_retry;

                case 'q': /* exit */
                    return 0;

                case '\r': /* step */
                case '\n':
                case '\0':
                    break;

                default:
                    upi42_log("Monitor commands:\n");
                    upi42_log("- Return (no command) - Step execution\n");
                    upi42_log("- q (or Ctrl+C) - Exit\n");
                    upi42_log("- gXXXX - Execute until PC is hex value XXXX\n");
                    upi42_log("- dXX - Write hex value XX to data port\n");
                    upi42_log("- cXX - Write hex value XX to command port\n");
                    upi42_log("- r - Read from data port and reset OBF\n");
skip_and_retry:
                    scanf("%*c", &cmd_buf);
                    goto retry;
            }
        }

        /* Execute a cycle. */
        upi42_exec(upi42);
    }

    return 0;
}
#else
static void
upi42_write(uint16_t port, uint8_t val, void *priv)
{
    upi42_t *upi42 = (upi42_t *) priv;
    uint32_t temp_type, uint8_t *temp_rom;
    int i;

    switch (port) {
        /* Write to data port. */
        case 0x0060:
        case 0x0160:
            upi42_dbb_write(0, val, upi42);
            break;

        /* RAM Index. */
        case 0x0162:
            upi42->ram_index = val & upi42->rammask;
            break;

        /* RAM. */
        case 0x0163:
            upi42->ram[upi42->ram_index & upi42->rammask] = val;
            break;

        /* Write to command port. */
        case 0x0064:
        case 0x0164:
            upi42_cmd_write(0, val, upi42);
            break;

        /* Input ports. */
        case 0x0180 ... 0x0187:
            upi42->ports_in[addr & 0x0007] = val;
            break;

        /* Output ports. */
        case 0x0188 ... 0x018f:
            upi42->ports_out[addr & 0x0007] = val;
            break;

        /* 4 = T0, 5 = T1. */
        case 0x0194:
            upi42->t0 = (val >> 4) & 0x01;
            upi42->t1 = (val >> 5) & 0x01;
            break;

        /* Program counter. */
        case 0x0196:
            upi42->pc = (upi42->pc & 0xff00) | val;
            break;
        case 0x0197:
            upi42->pc = (upi42->pc & 0x00ff) | (val << 8);
            break;

        /* Input data buffer. */
        case 0x019a:
            upi42->dbb_in = val;
            break;

        /* Output data buffer. */
        case 0x019b:
            upi42->dbb_out = val;
            break;

        /* ROM Index. */
        case 0x01a0:
            upi42->rom_index = (upi42->rom_index & 0xff00) | val;
            break;
        case 0x01a1:
            upi42->rom_index = (upi42->rom_index & 0x00ff) | (val << 8);
            break;

        /* Hard reset. */
        case 0x01a2:
            temp_type = upi42->type;
            temp_rom  = upi42->rom;
            upi42_do_init(temp_type, temp_rom);
            break;

        /* Soft reset. */
        case 0x01a3:
            upi42_reset(upi42);
            break;

        /* ROM. */
        case 0x01a4:
            upi42->rom[upi42->rom_index & upi42->rommask] = val;
            break;
        case 0x01a5:
            upi42->rom[(upi42->rom_index + 1) & upi42->rommask] = val;
            break;
        case 0x01a6:
            upi42->rom[(upi42->rom_index + 2) & upi42->rommask] = val;
            break;
        case 0x01a7:
            upi42->rom[(upi42->rom_index + 3) & upi42->rommask] = val;
            break;

        /* Pause. */
        case 0x01a8:
            break;

        /* Resume. */
        case 0x01a9:
            break;

        /* Bus master ROM: 0 = direction (0 = to memory, 1 = from memory). */
        case 0x01aa:
            if (val & 0x01) {
                for (i = 0; i <= upi42->rommask; i += 4)
                    *(uint32_t *) &(upi42->rom[i]) = mem_readl_phys(upi42->ram_addr + i);
            } else {
                for (i = 0; i <= upi42->rommask; i += 4)
                    mem_writel_phys(upi42->ram_addr + i, *(uint32_t *) &(upi42->rom[i]));
            }
            upi42->bm_stat = (val & 0x01) | 0x02;
            break;
    }
}

static uint8_t
upi42_read(uint16_t port, void *priv)
{
    upi42_t *upi42 = (upi42_t *) priv;
    uint8_t  ret   = 0xff;

    switch (port) {
        /* Type. */
        case 0x015c:
            ret = upi42->type & 0xff;
            break;
        case 0x015d:
            ret = upi42->type >> 8;
            break;
        case 0x015e:
            ret = upi42->type >> 16;
            break;
        case 0x015f:
            ret = upi42->type >> 24;
            break;

        /* Read from data port and reset OBF. */
        case 0x0060:
        case 0x0160:
            ret = upi42->dbb_out;
            upi42->sts &= ~0x01; /* clear OBF */
            break;

        /* RAM Mask. */
        case 0x0161:
            ret = upi42->rammask;
            break;

        /* RAM Index. */
        case 0x0162:
            ret = upi42->ram_index;
            break;

        /* RAM. */
        case 0x0163:
            ret = upi42->ram[upi42->ram_index & upi42->rammask];
            break;

        /* Read status. */
        case 0x0064:
        case 0x0164:
            ret = upi42->sts;
            break;

        /* Input ports. */
        case 0x0180 ... 0x0187:
            ret = upi42->ports_in[addr & 0x0007];
            break;

        /* Output ports. */
        case 0x0188 ... 0x018f:
            ret = upi42->ports_out[addr & 0x0007];
            break;

        /* Accumulator. */
        case 0x0190:
            ret = upi42->a;
            break;

        /* Timer counter. */
        case 0x0191:
            ret = upi42->t;
            break;

        /* Program status word. */
        case 0x0192:
            ret = upi42->psw;
            break;

        /* 0-4 = Prescaler, 5 = TF, 6 = Skip Timer Inc, 7 = Run Timer. */
        case 0x0193:
            ret = (upi42->prescaler & 0x1f) || ((upi42->tf & 0x01) << 5) || ((upi42->skip_timer_inc & 0x01) << 6) || ((upi42->run_timer & 0x01) << 7);
            break;

        /* 0 = I, 1 = I Raise, 2 = TCNTI Raise, 3 = IRQ Mask, 4 = T0, 5 = T1, 6 = Flags, 7 = DBF. */
        case 0x0194:
            ret = (upi42->i & 0x01) || ((upi42->i_raise & 0x01) << 1) || ((upi42->tcnti_raise & 0x01) << 2) || ((upi42->irq_mask & 0x01) << 3) || ((upi42->t0 & 0x01) << 4) || ((upi42->t1 & 0x01) << 5) || ((upi42->flags & 0x01) << 6) || ((upi42->dbf & 0x01) << 7);
            break;

        /* 0 = Suspend. */
        case 0x0195:
            ret = (upi42->suspend & 0x01);
            break;

        /* Program counter. */
        case 0x0196:
            ret = upi42->pc & 0xff;
            break;
        case 0x0197:
            ret = upi42->pc >> 8;
            break;

        /* ROM Mask. */
        case 0x0198:
            ret = upi42->rommask & 0xff;
            break;
        case 0x0199:
            ret = upi42->rommask >> 8;
            break;

        /* Input data buffer. */
        case 0x019a:
            ret = upi42->dbb_in;
            break;

        /* Output data buffer. */
        case 0x019b:
            ret = upi42->dbb_out;
            break;

        /* Cycle counter. */
        case 0x019c:
            ret = upi42->cycs & 0xff;
            break;
        case 0x019d:
            ret = upi42->cycs >> 8;
            break;
        case 0x019e:
            ret = upi42->cycs >> 16;
            break;
        case 0x019f:
            ret = upi42->cycs >> 24;
            break;

        /* ROM Index. */
        case 0x01a0:
            ret = upi42->rom_index & 0xff;
            break;
        case 0x01a1:
            ret = upi42->rom_index >> 8;
            break;

        /* ROM. */
        case 0x01a4:
            ret = upi42->rom[upi42->rom_index & upi42->rommask];
            break;
        case 0x01a5:
            ret = upi42->rom[(upi42->rom_index + 1) & upi42->rommask];
            break;
        case 0x01a6:
            ret = upi42->rom[(upi42->rom_index + 2) & upi42->rommask];
            break;
        case 0x01a7:
            ret = upi42->rom[(upi42->rom_index + 3) & upi42->rommask];
            break;

        /* Bus master status: 0 = direction, 1 = finished. */
        case 0x01ab:
            ret = upi42->bm_stat;
            break;
    }

    return ret;
}
#endif
