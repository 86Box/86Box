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
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2019-2020 Miran Grca.
 */
#ifndef EMU_PIT_H
#define EMU_PIT_H

#define NUM_COUNTERS 3

typedef struct ctr_t {
    uint8_t m;
    uint8_t ctrl;
    uint8_t read_status;
    uint8_t latch;
    uint8_t s1_det;
    uint8_t l_det;
    uint8_t bcd;
    uint8_t incomplete;

    uint16_t rl;

    int rm;
    int wm;
    int gate;
    int out;
    int newcount;
    int clock;
    int using_timer;
    int latched;
    int state;
    int null_count;
    int do_read_status;
    int enable;

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

    uint32_t l;
    uint32_t lback;
    uint32_t lback2;

    void (*load_func)(uint8_t new_m, int new_count);
    void (*out_func)(int new_out, int old_out, void *priv);
} ctr_t;

typedef struct PIT {
    int        flags;
    int        clock;
    pc_timer_t callback_timer;

    ctr_t counters[NUM_COUNTERS];

    uint8_t ctrl;

    uint64_t   pit_const;

    void *dev_priv;
} pit_t;

extern pit_t *ext_pit;

enum {
    PIT_8253      = 0,
    PIT_8254      = 1,
    PIT_8253_FAST = 2,
    PIT_8254_FAST = 3
};

typedef struct pit_intf_t {
    uint8_t (*read)(uint16_t addr, void *priv);
    void (*write)(uint16_t addr, uint8_t val, void *priv);
    /* Gets a counter's count. */
    uint16_t (*get_count)(void *data, int counter_id);
    /* Gets a counter's out. */
    int  (*get_outlevel)(void *data, int counter_id);
    /* Sets a counter's GATE input. */
    void (*set_gate)(void *data, int counter_id, int gate);
    /* Sets if a counter's CLOCK input is from the timer or not - used by PCjr. */
    void (*set_using_timer)(void *data, int counter_id, int using_timer);
    /* Sets a counter's OUT output handler. */
    void (*set_out_func)(void *data, int counter_id, void (*func)(int new_out, int old_out, void *priv));
    /* Sets a counter's load count handler. */
    void (*set_load_func)(void *data, int counter_id, void (*func)(uint8_t new_m, int new_count));
    void (*ctr_clock)(void *data, int counter_id);
    void (*set_pit_const)(void *data, uint64_t pit_const);
    void *data;
} pit_intf_t;

extern pit_intf_t       pit_devs[2];
extern const pit_intf_t pit_classic_intf;

extern double SYSCLK;
extern double PCICLK;
extern double AGPCLK;

extern uint64_t PITCONST;
extern uint64_t PAS16CONST;
extern uint64_t PAS16CONST2;
extern uint64_t PASSCSICONST;
extern uint64_t ISACONST;
extern uint64_t CGACONST;
extern uint64_t MDACONST;
extern uint64_t HERCCONST;
extern uint64_t VGACONST1;
extern uint64_t VGACONST2;
extern uint64_t RTCCONST;

extern int refresh_at_enable;

extern void pit_device_reset(pit_t *dev);

extern void pit_change_pas16_consts(double prescale);

extern void pit_set_pit_const(void *data, uint64_t pit_const);

extern void ctr_clock(void *data, int counter_id);

/* Sets a counter's CLOCK input. */
extern void pit_ctr_set_clock(ctr_t *ctr, int clock, void *priv);

extern void pit_ctr_set_gate(void *data, int counter_id, int gate);

extern void pit_ctr_set_out_func(void *data, int counter_id, void (*func)(int new_out, int old_out, void *priv));

extern void pit_ctr_set_using_timer(void *data, int counter_id, int using_timer);

extern pit_t *pit_common_init(int type, void (*out0)(int new_out, int old_out, void *priv), void (*out1)(int new_out, int old_out, void *priv));
extern pit_t *pit_ps2_init(int type);
extern void   pit_reset(pit_t *dev);

extern void pit_irq0_timer_ps2(int new_out, int old_out, void *priv);

extern void pit_refresh_timer_xt(int new_out, int old_out, void *priv);
extern void pit_refresh_timer_at(int new_out, int old_out, void *priv);

extern void pit_speaker_timer(int new_out, int old_out, void *priv);

extern void pit_nmi_timer_ps2(int new_out, int old_out, void *priv);

extern void pit_set_clock(uint32_t clock);
extern void pit_handler(int set, uint16_t base, int size, void *priv);

extern uint8_t pit_read_reg(void *priv, uint8_t reg);

#ifdef EMU_DEVICE_H
extern const device_t i8253_device;
extern const device_t i8253_ext_io_device;
extern const device_t i8254_device;
extern const device_t i8254_sec_device;
extern const device_t i8254_ext_io_device;
extern const device_t i8254_ps2_device;
#endif

#endif /*EMU_PIT_H*/
