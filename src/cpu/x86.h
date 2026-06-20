/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Second CPU header.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          leilei,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2018 leilei.
 *          Copyright 2016-2020 Miran Grca.
 */
#ifndef EMU_X86_H
#define EMU_X86_H

#define ABRT_MASK 0x3f
/*An 'expected' exception is one that would be expected to occur on every execution
  of this code path; eg a GPF due to being in v86 mode. An 'unexpected' exception is
  one that would be unlikely to occur on the next exception, eg a page fault may be
  fixed up by the exception handler and the next execution would not hit it.

  This distinction is used by the dynarec; a block that hits an 'expected' exception
  would be compiled, a block that hits an 'unexpected' exception would be rejected so
  that we don't end up with an unnecessarily short block*/
#define ABRT_EXPECTED 0x80

extern uint8_t opcode;

extern uint8_t flags_p;
extern uint8_t znptable8[256];

extern uint16_t  last_addr;
extern uint16_t  zero;
extern uint16_t  oldcs;
extern uint16_t  lastcs;
extern uint16_t  lastpc;
extern uint16_t *mod1add[2][8];
extern uint16_t  znptable16[65536];

extern int pfq_pos;
extern int x86_was_reset;
extern int trap;
extern int codegen_flat_ss;
extern int codegen_flat_ds;
extern int timetolive;
extern int keyboardtimer;
extern int trap;
extern int optype;
extern int stack32;
extern int oldcpl;
extern int cpl_override;
extern int nmi_enable;
extern int oddeven;
extern int inttype;

extern int cpu_init;

extern uint32_t  use32;
extern uint32_t  rmdat;
extern uint32_t  easeg;
extern uint32_t  oxpc;
extern uint32_t  flags_zn;
extern uint32_t  abrt_error;
extern uint32_t  backupregs[16];
extern uint32_t *mod1seg[8];
extern uint32_t *eal_r;
extern uint32_t *eal_w;

extern int fpu_cycles;

#define fetchdat  rmdat

#define setznp168 setznp16

#define getr8(r)  ((r & 4) ? cpu_state.regs[r & 3].b.h : cpu_state.regs[r & 3].b.l)
#define getr16(r) cpu_state.regs[r].w
#define getr32(r) cpu_state.regs[r].l

#define setr8(r, v)                    \
    if (r & 4)                         \
        cpu_state.regs[r & 3].b.h = v; \
    else                               \
        cpu_state.regs[r & 3].b.l = v;
#define setr16(r, v) cpu_state.regs[r].w = v
#define setr32(r, v) cpu_state.regs[r].l = v

#define fetchea()                  \
    {                              \
        rmdat = readmemb(cs + pc); \
        pc++;                      \
        reg = (rmdat >> 3) & 7;    \
        mod = (rmdat >> 6) & 3;    \
        rm  = rmdat & 7;           \
        if (mod != 3)              \
            fetcheal();            \
    }

extern void x86illegal(void);

extern uint8_t rep_op;
extern uint8_t is_smint;

extern uint16_t io_port;
extern uint32_t io_val;

#endif /*EMU_X86_H*/
