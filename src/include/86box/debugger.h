/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the built-in debugger core (memory viewer).
 */
#ifndef EMU_DEBUGGER_H
#define EMU_DEBUGGER_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Execution states. */
enum {
    DEBUGGER_RUN   = 0,
    DEBUGGER_STEP  = 1, /* running debugger_steps more instructions */
    DEBUGGER_BREAK = 2  /* stopped at an instruction boundary */
};

/* Why the processor last stopped. */
enum {
    DEBUGGER_STOP_NONE  = 0,
    DEBUGGER_STOP_PAUSE = 1,
    DEBUGGER_STOP_STEP  = 2,
    DEBUGGER_STOP_BREAKPOINT = 3
};

/* What a byte of memory is to the viewer. */
enum {
    DEBUGGER_MEM_UNMAPPED     = 0, /* nothing answers there */
    DEBUGGER_MEM_RAM          = 1, /* system RAM */
    DEBUGGER_MEM_ROM          = 2, /* mapped, not RAM (ROM and the like), read straight from its backing */
    DEBUGGER_MEM_INACCESSIBLE = 3, /* only a device's handlers reach it, or the linear page is absent */
    DEBUGGER_MEM_VIDEO        = 4  /* a video card's memory, reached without its handlers' side effects */
};

/* Address spaces. */
enum {
    DEBUGGER_SPACE_PHYSICAL = 0,
    DEBUGGER_SPACE_LINEAR   = 1
};

#define DEBUGGER_MAX_BREAKPOINTS 32

extern volatile int debugger_attached;

/* Called by the instruction loops after each pass while attached; nonzero
   makes the loop return. partial: the pass did not finish the instruction
   (the 8086 and V30 loops run REP and HLT one pass at a time). */
extern int debugger_instruction(int partial);

#define DEBUGGER_INSTRUCTION()          (debugger_attached && debugger_instruction(0))
#define DEBUGGER_INSTRUCTION_PASS(done) (debugger_attached && debugger_instruction(!(done)))

/* Run a block of the processor's cycles, as cpu_exec() would, but stopped
   while the debugger holds it, and through the interpreter while attached. */
extern void debugger_cpu_exec(int32_t cycs);

extern void debugger_attach(int attach);
extern int  debugger_get_state(void);
/* Attached and holding the processor. */
extern int  debugger_stopped(void);
/* The processor stopped in the middle of a REP string instruction or HLT. */
extern int  debugger_stopped_partial(void);
/* Around a hard reset, which frees the memory the viewer reads: the viewer
   leaves memory alone while debugger_resetting() says so. */
extern void debugger_reset_begin(void);
extern void debugger_reset_end(void);
extern int  debugger_resetting(void);
extern void debugger_pause(void);
extern void debugger_continue(void);
extern void debugger_step(int count);
extern int  debugger_stop_reason(void);
/* How many times the processor has stopped so far. */
extern uint32_t debugger_stop_count(void);

extern int  debugger_breakpoint_toggle(uint32_t linear);
extern int  debugger_breakpoint_present(uint32_t linear);
extern int  debugger_breakpoints(uint32_t *list, int max);
extern void debugger_breakpoints_clear(void);

/* Memory: these never call a device's handlers and never set accessed or
   dirty bits; bytes they cannot reach that way are classed, not read. Only
   the len bytes asked for are touched (and, for linear addresses, the page
   table entries that map them). cls may be NULL. */
extern void debugger_mem_read(int space, uint32_t addr, uint32_t len, uint8_t *data, uint8_t *cls);
/* Write one byte, to RAM or video memory only; returns nonzero on success. */
extern int debugger_mem_write(int space, uint32_t addr, uint8_t val);
/* Linear to physical, by a side-effect-free page walk; nonzero on success. */
extern int debugger_translate(uint32_t linear, uint32_t *phys);
/* The linear base of a segment, from a register name's cached base, or a
   selector looked up in the GDT/LDT as the processor is now. */
extern int debugger_selector_base(uint16_t sel, uint32_t *base);

/* The processor, as it is now. */
typedef struct debugger_cpu_info_t {
    uint32_t pc_linear; /* CS base + IP/EIP of the next instruction */
    uint32_t ip;        /* IP/EIP of the next instruction */
    uint32_t cs_base;
    uint16_t cs_sel;
    int      code32; /* the code segment is 32-bit */
    int      protected_mode;
    int      v86;
} debugger_cpu_info_t;

extern void debugger_cpu_info(debugger_cpu_info_t *info);

/* What the disassembler needs to decode as the emulated processor does. */
enum {
    DEBUGGER_ISA_8086 = 0,
    DEBUGGER_ISA_80186,
    DEBUGGER_ISA_V30,   /* NEC V20/V30 in native mode */
    DEBUGGER_ISA_I8080, /* NEC V20/V30 in 8080 emulation mode */
    DEBUGGER_ISA_286,
    DEBUGGER_ISA_386    /* the 386 and later */
};

enum {
    DEBUGGER_FPU_NONE = 0,
    DEBUGGER_FPU_8087,
    DEBUGGER_FPU_287,
    DEBUGGER_FPU_387 /* the 387 and later, and on-chip FPUs */
};

typedef struct debugger_decode_info_t {
    int isa;
    int fpu;
    int code32;   /* CS is a 32-bit code segment */
    int stack32;  /* SS is a 32-bit stack segment */
    int wrap16;   /* instruction fetches wrap at 64 KB within CS */
    int fpu_trap; /* CR0.EM or TS set: x87 opcodes raise #NM */
    int vpc;      /* Virtual PC extensions (0F 3F) are on */
    int fpu_ud;   /* an x87 opcode the coprocessor lacks raises #UD (the 486 and later) */
} debugger_decode_info_t;

extern void debugger_decode_info(debugger_decode_info_t *info);

/* What the 286 and later's opcode tables run for an opcode (map 0: one byte,
   map 1: after 0F), with opsize holding 0x100 for 32-bit operands and 0x200
   for 32-bit addresses, as the tables are indexed. */
enum {
    DEBUGGER_OP_ILLEGAL = 0, /* #UD */
    DEBUGGER_OP_NORMAL,      /* as documented for the instruction set */
    DEBUGGER_OP_LOCK,        /* F1 on the 286: LOCK */
    DEBUGGER_OP_LOADALL,     /* 0F 05 on the 286 */
    DEBUGGER_OP_LOADALL386,  /* 0F 07 on the 386 and 486 */
    DEBUGGER_OP_UMOV,        /* 0F 10-13 */
    DEBUGGER_OP_RDSHR,       /* Cyrix 0F 36 */
    DEBUGGER_OP_WRSHR,       /* Cyrix 0F 37 */
    DEBUGGER_OP_SVDC,        /* Cyrix 0F 78 */
    DEBUGGER_OP_RSDC,        /* Cyrix 0F 79 */
    DEBUGGER_OP_SVLDT,       /* Cyrix 0F 7A */
    DEBUGGER_OP_RSLDT,       /* Cyrix 0F 7B */
    DEBUGGER_OP_SVTS,        /* Cyrix 0F 7C */
    DEBUGGER_OP_RSTS,        /* Cyrix 0F 7D */
    DEBUGGER_OP_SMINT,       /* Cyrix 0F 7E, MediaGX 0F 38 */
    DEBUGGER_OP_BB0_RESET,   /* MediaGX 0F 3A */
    DEBUGGER_OP_BB1_RESET,   /* MediaGX 0F 3B */
    DEBUGGER_OP_CPU_WRITE,   /* MediaGX 0F 3C */
    DEBUGGER_OP_CPU_READ,    /* MediaGX 0F 3D */
    DEBUGGER_OP_VPCEXT,      /* Virtual PC 0F 3F */
    DEBUGGER_OP_HINT_NOP     /* P6 0F 0D, 0F 18-1F */
};

extern int debugger_opcode_kind(int map, uint8_t op, int opsize);
/* The same for an x87 escape (D8-DF) and its ModR/M byte, on the 286 and
   later with a coprocessor: DEBUGGER_OP_ILLEGAL or DEBUGGER_OP_NORMAL. */
extern int debugger_fpu_kind(uint8_t op, uint8_t modrm, int addr32);
/* Whether the processor takes a LOCK prefix on the instruction whose opcode
   and next two bytes these are (zeros where the instruction has none). */
extern int debugger_lock_legal(uint8_t op, uint8_t b1, uint8_t b2);
/* Whether the V20/V30 core decodes a ModR/M byte for opcode 0F op. */
extern int debugger_nec_0f_modrm(uint8_t op);
/* The bytes the 8086-class prefetch queue already holds for the instruction
   at CS:IP, which the processor runs instead of memory's (0 on the 286 and
   later). */
extern int debugger_prefetch_queue(uint8_t *buf, int max);
/* A textual dump of the processor's registers. */
extern void debugger_cpu_format(char *buf, int size);
/* A register's value by name ("eax", "ip", "cs" (selector), ...); nonzero
   on success. seg_base gets a segment register's cached base. */
extern int debugger_register(const char *name, uint32_t *val, uint32_t *seg_base);

#ifdef __cplusplus
}
#endif

#endif /*EMU_DEBUGGER_H*/
