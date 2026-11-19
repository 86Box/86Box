/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          x86 CPU segment emulation common parts header.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2017 Miran Grca.
 */
#ifndef EMU_X86SEG_COMMON_H
#define EMU_X86SEG_COMMON_H

#define JMP        1
#define CALL       2
#define IRET       3
#define OPTYPE_INT 4

enum {
    ABRT_NONE = 0,
    ABRT_GEN  = 1,
    ABRT_TS   = 0xA,
    ABRT_NP   = 0xB,
    ABRT_SS   = 0xC,
    ABRT_GPF  = 0xD,
    ABRT_PF   = 0xE,
    ABRT_DE   = 0x40 /* INT 0, but we have to distinguish it from ABRT_NONE. */
};

extern uint8_t opcode2;

extern int     cgate16;
extern int     cgate32;
extern int     cgate_task;

extern int     intgatesize;

typedef struct call_gate_undo_t {
    x86seg   stack_seg;
    x86seg   code_seg;
    uint32_t esp;
    int      stack32;
    uint32_t use32;
    uint32_t status;
    int      oldcpl;
    int      active;
} call_gate_undo_t;

extern call_gate_undo_t call_gate_undo;

extern void    call_gate_undo_save(void);
extern void    call_gate_undo_restore(void);

extern void    x86seg_reset(void);
extern void    x86gen(void);
extern void    x86de(char *s, uint16_t error);
extern void    x86gpf(char *s, uint16_t error);
extern void    x86gpf_expected(char *s, uint16_t error);
extern void    x86np(char *s, uint16_t error);
extern void    x86ss(char *s, uint16_t error);
extern void    x86ts(char *s, uint16_t error);
extern void    do_seg_load(x86seg *s, uint16_t *segdat);

#endif /*EMU_X86SEG_COMMON_H*/
