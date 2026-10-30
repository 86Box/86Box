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
    DEBUGGER_MEM_INACCESSIBLE = 3  /* only a device's handlers reach it, or the linear page is absent */
};

/* Address spaces. */
enum {
    DEBUGGER_SPACE_PHYSICAL = 0,
    DEBUGGER_SPACE_LINEAR   = 1
};

#define DEBUGGER_MAX_BREAKPOINTS 32

extern volatile int debugger_attached;
extern volatile int debugger_state;

/* Called by the instruction loops after each instruction while attached;
   nonzero makes the loop return. */
extern int debugger_instruction(void);

#define DEBUGGER_INSTRUCTION() (debugger_attached && debugger_instruction())

/* Run a block of the processor's cycles, as cpu_exec() would, but stopped
   while the debugger holds it, and through the interpreter while attached. */
extern void debugger_cpu_exec(int32_t cycs);

extern void debugger_attach(int attach);
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
/* Write one byte, to RAM only; returns nonzero on success. */
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
/* A textual dump of the processor's registers. */
extern void debugger_cpu_format(char *buf, int size);
/* A register's value by name ("eax", "ip", "cs" (selector), ...); nonzero
   on success. seg_base gets a segment register's cached base. */
extern int debugger_register(const char *name, uint32_t *val, uint32_t *seg_base);

#ifdef __cplusplus
}
#endif

#endif /*EMU_DEBUGGER_H*/
