/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the GDB stub server.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2022 RichardG.
 */
#ifndef EMU_GDBSTUB_H
#define EMU_GDBSTUB_H
#include <stdint.h>
#include <86box/mem.h>

#define GDBSTUB_MEM_READ   0
#define GDBSTUB_MEM_WRITE  16
#define GDBSTUB_MEM_AWATCH 32

enum {
    GDBSTUB_EXEC         = 0,
    GDBSTUB_SSTEP        = 1,
    GDBSTUB_BREAK        = 2,
    GDBSTUB_BREAK_SW     = 3,
    GDBSTUB_BREAK_HW     = 4,
    GDBSTUB_BREAK_RWATCH = 5,
    GDBSTUB_BREAK_WWATCH = 6,
    GDBSTUB_BREAK_AWATCH = 7
};

#ifdef USE_GDBSTUB

#    define GDBSTUB_MEM_ACCESS(addr, access, width)                                   \
        uint32_t gdbstub_page = (addr) >> MEM_GRANULARITY_BITS;                       \
        if (gdbstub_watch_pages[gdbstub_page >> 6] & (1ULL << (gdbstub_page & 63))) { \
            uint32_t gdbstub_addrs[(width)];                                          \
            for (int gdbstub_i = 0; gdbstub_i < (width); gdbstub_i++)                 \
                gdbstub_addrs[gdbstub_i] = (addr) + gdbstub_i;                        \
            gdbstub_mem_access(gdbstub_addrs, (access) | (width));                    \
        }

#    define GDBSTUB_MEM_ACCESS_FAST(addrs, access, width)                           \
        uint32_t gdbstub_page = (addrs)[0] >> MEM_GRANULARITY_BITS;                 \
        if (gdbstub_watch_pages[gdbstub_page >> 6] & (1ULL << (gdbstub_page & 63))) \
            gdbstub_mem_access((addrs), (access) | (width));

extern int      gdbstub_step, gdbstub_next_asap;
extern uint64_t gdbstub_watch_pages[(((uint32_t) -1) >> (MEM_GRANULARITY_BITS + 6)) + 1];

extern void gdbstub_cpu_init(void);
extern int  gdbstub_instruction(void);
extern int  gdbstub_int3(void);
extern void gdbstub_mem_access(uint32_t *addrs, int access);
extern void gdbstub_init(void);
extern void gdbstub_close(void);

#else

#    define GDBSTUB_MEM_ACCESS(addr, access, width)
#    define GDBSTUB_MEM_ACCESS_FAST(addrs, access, width)

#    define gdbstub_step      0
#    define gdbstub_next_asap 0

#    define gdbstub_cpu_init()
#    define gdbstub_instruction() 0
#    define gdbstub_int3()        0
#    define gdbstub_init()
#    define gdbstub_close()

#endif

#endif
