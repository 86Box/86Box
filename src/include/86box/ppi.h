/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Port 61h/speaker state shared by the PIT and the system boards,
 *          and the Intel 8255(A) Programmable Peripheral Interface.
 *
 *          The 8255 is ported from MAME's machine/i8255.cpp
 *          (BSD-3-Clause, copyright holders: Curt Coder).
 */
#ifndef EMU_PPI_H
#define EMU_PPI_H

/* Port 61h and speaker state. Despite the name this is not an 8255: AT and
   later machines have only a latch there. Boards with a real 8255 mirror
   their port B into it. */
typedef struct PPI {
    int     s2;
    uint8_t pa;
    uint8_t pb;
} PPI;

extern int ppispeakon;
extern PPI ppi;

extern void ppi_reset(void);

/* Intel 8255(A) PPI, modes 0, 1 and 2.

   The owner decodes its own I/O range and calls i8255_read()/i8255_write()
   with the register index (0 = port A, 1 = port B, 2 = port C, 3 = control).

   Port pins go through per-port callbacks sharing one priv pointer:
   - in:  pin levels when the port (or port C bits) are inputs; unset = 0.
   - out: called with the pin levels whenever an output changes. A port
          programmed as input drives its tri-state value instead.
   - tri: what an input port's pins float to; unset = 0xff.

   Handshake inputs (modes 1 and 2) come in through i8255_pc2_w(),
   i8255_pc4_w() and i8255_pc6_w(); i8255_acka_r()/i8255_ackb_r() are the
   peripheral's strobed reads of an output port. */

enum {
    I8255_PORT_A = 0,
    I8255_PORT_B = 1,
    I8255_PORT_C = 2
};

/* Variant flags (the Amstrad AMS40489 sets all three). */
#define I8255_FORCE_PORTB_IN           0x01
#define I8255_FORCE_PORTC_OUT          0x02
#define I8255_DONT_CLEAR_OUTPUT_LATCHES 0x04

typedef struct i8255_t {
    uint8_t flags;

    uint8_t control;   /* mode control word */
    uint8_t output[3]; /* output latch */
    uint8_t input[3];  /* input latch */
    int     ibf[2];    /* input buffer full flag */
    int     obf[2];    /* output buffer full flag, negative logic */
    int     inte[2];   /* interrupt enable */
    int     inte1;     /* mode 2 output interrupt enable */
    int     inte2;     /* mode 2 input interrupt enable */
    int     intr[2];   /* interrupt */

    uint8_t (*in[3])(void *priv);
    void (*out[3])(uint8_t val, void *priv);
    uint8_t (*tri[3])(void *priv);
    void *priv;
} i8255_t;

extern void i8255_set_priv(i8255_t *dev, void *priv);
extern void i8255_set_flags(i8255_t *dev, uint8_t flags);
extern void i8255_set_in(i8255_t *dev, int port, uint8_t (*func)(void *priv));
extern void i8255_set_out(i8255_t *dev, int port, void (*func)(uint8_t val, void *priv));
extern void i8255_set_tri(i8255_t *dev, int port, uint8_t (*func)(void *priv));

extern void    i8255_reset(i8255_t *dev);
extern uint8_t i8255_read(i8255_t *dev, uint8_t reg);
extern void    i8255_write(i8255_t *dev, uint8_t reg, uint8_t val);

extern uint8_t i8255_pa_r(i8255_t *dev);
extern uint8_t i8255_acka_r(i8255_t *dev);
extern uint8_t i8255_pb_r(i8255_t *dev);
extern uint8_t i8255_ackb_r(i8255_t *dev);
extern void    i8255_pc2_w(i8255_t *dev, int state);
extern void    i8255_pc4_w(i8255_t *dev, int state);
extern void    i8255_pc6_w(i8255_t *dev, int state);

#ifdef EMU_DEVICE_H
extern const device_t i8255_device;
#endif

#endif /*EMU_PPI_H*/
