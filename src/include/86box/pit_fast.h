/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header of the implementation of the Intel 8253/8254
 *          Programmable Interval Timer.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2019-2020 Miran Grca.
 */

#ifndef EMU_PIT_FAST_H
#define EMU_PIT_FAST_H

typedef struct ctrf_t {
    uint8_t m;
    uint8_t ctrl;
    uint8_t read_status;
    uint8_t latch;
    uint8_t bcd;

    uint16_t rl;

    int rm;
    int wm;
    int gate;
    int out;
    int newcount;
    int clock;
    int using_timer;
    int latched;
    int do_read_status;
    int enabled;
    int disabled;
    int initial;
    int thit;
    int running;
    int rereadlatch;

    union {
        int32_t count;
        struct {
            int32_t units     : 4;
            int32_t tens      : 4;
            int32_t hundreds  : 4;
            int32_t thousands : 4;
            int32_t myriads   : 4;
        };
    };

    uint32_t   l;
    pc_timer_t timer;

    void (*load_func)(uint8_t new_m, int new_count);
    void (*out_func)(int new_out, int old_out);
} ctrf_t;

typedef struct pitf_t {
    int    flags;
    ctrf_t counters[3];

    uint8_t ctrl;
} pitf_t;

extern uint8_t pitf_read_reg(void *priv, uint8_t reg);

extern const pit_intf_t pit_fast_intf;

#ifdef EMU_DEVICE_H
extern const device_t i8253_fast_device;
extern const device_t i8254_fast_device;
extern const device_t i8254_sec_fast_device;
extern const device_t i8254_ext_io_fast_device;
extern const device_t i8254_ps2_fast_device;
#endif

#endif /*EMU_PIT_FAST_H*/
