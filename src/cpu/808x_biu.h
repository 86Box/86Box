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

#define DEBUG_SEG 0x0000
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
#define DEBUG_OFF_L 0x1000
#define DEBUG_OFF_H 0x1364

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

/* Temporary BIU externs - move to 808x_biu.h. */
extern void        biu_resume_on_queue_read(void);
extern void        wait(int c);
extern void        biu_reset(void);
extern void        clock_end(void);
extern void        clock_start(void);
extern void        process_timers(void);
extern void        cycles_forward(int c);
extern void        cpu_io(int bits, int out, uint16_t port);
extern uint8_t     readmemb(uint32_t s, uint16_t a);
extern uint16_t    readmemw(uint32_t s, uint16_t a);
extern uint16_t    readmem(uint32_t s);
extern uint32_t    readmeml(uint32_t s, uint16_t a);
extern uint64_t    readmemq(uint32_t s, uint16_t a);
extern void        writememb(uint32_t s, uint32_t a, uint8_t v);
extern void        writememw(uint32_t s, uint32_t a, uint16_t v);
extern void        writemem(uint32_t s, uint16_t v);
extern void        writememl(uint32_t s, uint32_t a, uint32_t v);
extern void        writememq(uint32_t s, uint32_t a, uint64_t v);
extern uint8_t     pfq_read(void);
extern uint8_t     pfq_fetchb_common(void);
extern uint8_t     pfq_fetchb(void);
extern uint16_t    pfq_fetchw(void);
extern uint16_t    pfq_fetch(void);
extern void        biu_update_pc(void);
extern void        biu_queue_flush(void);
extern void        biu_suspend_fetch(void);
extern void        biu_begin_eu(void);
extern void        biu_wait_for_read_finish(void);

extern uint8_t     biu_preload_byte;

extern uint16_t    last_addr;
extern uint16_t    pfq_ip;

extern int         nx;

extern int         pfq_pos;
extern int         schedule_fetch;
extern int         in_lock;
extern int         bus_request_type;
extern int         pfq_size;
extern int         pic_data;
extern int         biu_queue_preload;

#endif /*EMU_808X_BIU_H*/
