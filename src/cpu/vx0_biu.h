/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          808x BIU emulation header.
 *
 * Authors: Andrew Jenner, <https://www.reenigne.org>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2015-2020 Andrew Jenner.
 *          Copyright 2016-2020 Miran Grca.
 */
#ifndef EMU_808X_BIU_H
#define EMU_808X_BIU_H

#define DEBUG_SEG 0xf000
// #define DEBUG_SEG 0x0f3c
// #define DEBUG_SEG 0x1e1f
// #define DEBUG_SEG 0xf000
// #define DEBUG_SEG 0xc800
// #define DEBUG_SEG 0x0070
// #define DEBUG_SEG 0x0291
// #define DEBUG_SEG 0xefff
// #define DEBUG_SEG 0x15a2

// #define DEBUG_OFF_L 0x2c3b
// #define DEBUG_OFF_L 0xe182
// #define DEBUG_OFF_L 0xf000
// #define DEBUG_OFF_H 0xefff
// #define DEBUG_OFF_L 0x0000
// #define DEBUG_OFF_H 0xffff
#define DEBUG_OFF_L 0xf300
#define DEBUG_OFF_H 0xf3ff

#define BUS_OUT         1
#define BUS_HIGH        2
#define BUS_WIDE        4
#define BUS_CODE        8
#define BUS_IO          16
#define BUS_MEM         32
#define BUS_PIC         64
#define BUS_ACCESS_TYPE (BUS_CODE | BUS_IO | BUS_MEM | BUS_PIC)

#undef readmemb
#undef readmemw
#undef readmeml
#undef readmemq

enum {
    BUS_T1 = 0,
    BUS_T2,
    BUS_T3,
    BUS_T4
};

enum {
    BIU_STATE_IDLE,
    BIU_STATE_SUSP,
    BIU_STATE_DELAY,
    BIU_STATE_RESUME,
    BIU_STATE_WAIT,
    BIU_STATE_PF,
    BIU_STATE_EU
};

enum {
    DMA_STATE_IDLE,
    DMA_STATE_TIMER,
    DMA_STATE_DREQ,
    DMA_STATE_HRQ,
    DMA_STATE_HLDA,
    DMA_STATE_OPERATING
};

extern void        execx86_instruction(void);

extern void        biu_resume_on_queue_read(void);
extern void        wait_vx0(int c);
extern void        biu_reset(void);
extern void        cpu_io_vx0(int bits, int out, uint16_t port);
extern void        biu_state_set_eu(void);
extern uint8_t     readmemb_vx0(uint32_t s, uint16_t a);
extern uint16_t    readmemw_vx0(uint32_t s, uint16_t a);
extern uint16_t    readmem_vx0(uint32_t s);
extern uint32_t    readmeml_vx0(uint32_t s, uint16_t a);
extern uint64_t    readmemq_vx0(uint32_t s, uint16_t a);
extern void        writememb_vx0(uint32_t s, uint32_t a, uint8_t v);
extern void        writememw_vx0(uint32_t s, uint32_t a, uint16_t v);
extern void        writemem_vx0(uint32_t s, uint16_t v);
extern void        writememl_vx0(uint32_t s, uint32_t a, uint32_t v);
extern void        writememq_vx0(uint32_t s, uint32_t a, uint64_t v);
extern uint8_t     biu_pfq_read(void);
extern uint8_t     biu_pfq_fetchb_common(void);
extern uint8_t     biu_pfq_fetchb(void);
extern uint16_t    biu_pfq_fetchw(void);
extern uint16_t    biu_pfq_fetch(void);
extern void        biu_update_pc(void);
extern void        biu_queue_flush(void);
extern void        biu_suspend_fetch(void);
extern void        biu_begin_eu(void);
extern void        biu_wait_for_read_finish(void);

extern uint8_t     biu_preload_byte;

extern int         nx;
extern int         oldc;
extern int         cpu_alu_op;
extern int         completed;
extern int         in_rep;
extern int         repeating;
extern int         rep_c_flag;
extern int         noint;
extern int         tempc_fpu;
extern int         clear_lock;
extern int         is_new_biu;

extern int         schedule_fetch;
extern int         in_lock;
extern int         bus_request_type;
extern int         pic_data;
extern int         biu_queue_preload;

extern uint32_t    cpu_src;
extern uint32_t    cpu_dest;

extern uint32_t    cpu_data;

extern uint32_t   *ovr_seg;

/* Pointer tables needed for segment overrides. */
extern uint32_t *  opseg[4];
extern x86seg   *  _opseg[4];

#endif /*EMU_808X_BIU_H*/
