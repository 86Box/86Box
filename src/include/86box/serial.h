/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the NS8250/16450/16550/16650/16750/16850/16950
 *          UART emulation.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 */

#ifndef EMU_SERIAL_H
#define EMU_SERIAL_H

#define SERIAL_8250      0
#define SERIAL_8250_PCJR 1
#define SERIAL_16450     2
#define SERIAL_16550     3
#define SERIAL_16650     4
#define SERIAL_16750     5
#define SERIAL_16850     6
#define SERIAL_16950     7

#define SERIAL_FIFO_SIZE 16

/* Default settings for the standard ports. */
#define COM1_ADDR 0x03f8
#define COM1_IRQ  4
#define COM2_ADDR 0x02f8
#define COM2_IRQ  3
#define COM3_ADDR 0x03e8
#define COM3_IRQ  4
#define COM4_ADDR 0x02e8
#define COM4_IRQ  3
// The following support being assingned IRQ 3, 4, 5, 9, 10, 11, 12 or 15
// There doesn't appear to be any specific standard however
// So defaults have been chosen arbitarily
// TODO: Allow configuration of the IRQ in the UI
//#define COM5_ADDR 0x03f0
//#define COM5_IRQ  3
#define COM5_ADDR 0x02f0
#define COM5_IRQ  11
#define COM6_ADDR 0x03e0
#define COM6_IRQ  10
#define COM7_ADDR 0x02e0
#define COM7_IRQ  9

struct serial_device_s;
struct serial_s;

typedef struct serial_s {
    uint8_t lsr;
    uint8_t thr;
    uint8_t mctrl;
    uint8_t rcr;
    uint8_t iir;
    uint8_t ier;
    uint8_t lcr;
    uint8_t msr;
    uint8_t dat;
    uint8_t int_status;
    uint8_t scratch;
    uint8_t fcr;
    uint8_t irq;
    uint8_t type;
    uint8_t inst;
    uint8_t transmit_enabled;
    uint8_t fifo_enabled;
    uint8_t bits;
    uint8_t data_bits;
    uint8_t baud_cycles;
    uint8_t txsr;
    uint8_t txsr_empty;
    uint8_t msr_set;
    uint8_t irq_state;

    uint16_t dlab;
    uint16_t base_address;
    uint16_t out_new;
    uint16_t thr_empty;

    void *rcvr_fifo;
    void *xmit_fifo;

    pc_timer_t transmit_timer;
    pc_timer_t timeout_timer;
    pc_timer_t receive_timer;
    double     clock_src;
    double     transmit_period;

    struct serial_device_s *sd;
} serial_t;

typedef struct serial_device_s {
    void    (*rcr_callback)(struct serial_s *serial, void *priv);
    void    (*dtr_callback)(struct serial_s *serial, int status, void *priv);
    void    (*dev_write)(struct serial_s *serial, void *priv, uint8_t data);
    void    (*lcr_callback)(struct serial_s *serial, void *priv, uint8_t lcr);
    void    (*transmit_period_callback)(struct serial_s *serial, void *priv, double transmit_period);
    void     *priv;
    serial_t *serial;
} serial_device_t;

typedef struct serial_port_s {
    uint8_t enabled;
} serial_port_t;

extern serial_port_t com_ports[SERIAL_MAX];

extern serial_t *serial_attach_ex(int port,
                                  void (*rcr_callback)(struct serial_s *serial, void *priv),
                                  void (*dev_write)(struct serial_s *serial, void *priv, uint8_t data),
                                  void (*transmit_period_callback)(struct serial_s *serial, void *priv, double transmit_period),
                                  void (*lcr_callback)(struct serial_s *serial, void *priv, uint8_t data_bits),
                                  void *priv);

extern serial_t *serial_attach_ex_2(int port,
                                  void (*rcr_callback)(struct serial_s *serial, void *priv),
                                  void (*dev_write)(struct serial_s *serial, void *priv, uint8_t data),
                                  void (*dtr_callback)(struct serial_s *serial, int status, void *priv),
                                  void *priv);

#define serial_attach(port, rcr_callback, dev_write, priv) \
        serial_attach_ex(port, rcr_callback, dev_write, NULL, NULL, priv);

extern void      serial_remove(serial_t *dev);
extern void      serial_set_type(serial_t *dev, int type);
extern void      serial_setup(serial_t *dev, uint16_t addr, uint8_t irq);
extern void      serial_irq(serial_t *dev, uint8_t irq);
extern void      serial_clear_fifo(serial_t *dev);
extern void      serial_write_fifo(serial_t *dev, uint8_t dat);
extern void      serial_set_next_inst(int ni);
extern void      serial_standalone_init(void);
extern void      serial_set_clock_src(serial_t *dev, double clock_src);
extern void      serial_reset_port(serial_t *dev);
extern uint8_t   serial_read(uint16_t addr, void *priv);
extern void      serial_device_timeout(void *priv);
extern void      serial_set_cts(serial_t *dev, uint8_t enabled);
extern void      serial_set_dsr(serial_t *dev, uint8_t enabled);
extern void      serial_set_dcd(serial_t *dev, uint8_t enabled);
extern void      serial_set_ri(serial_t *dev, uint8_t enabled);
extern int       serial_get_ri(serial_t *dev);

extern const device_t ns8250_device;
extern const device_t ns8250_pcjr_device;
extern const device_t ns16450_device;
extern const device_t ns16550_device;
extern const device_t ns16650_device;
extern const device_t ns16750_device;
extern const device_t ns16850_device;
extern const device_t ns16950_device;

#endif /*EMU_SERIAL_H*/
