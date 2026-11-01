/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the serial subsystem: the UART chips, the
 *          backend seam between a chip and whatever is on the other
 *          side of the port, and the host-side port table.
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

#include <86box/char.h>
#include <86box/device.h>

/* ------------------------------------------------------------------------ */
/* Port definitions                                                         */
/* ------------------------------------------------------------------------ */

#define SERIAL_8250          0
#define SERIAL_8250_PCJR_3F8 1
#define SERIAL_8250_PCJR_2F8 2
#define SERIAL_16450         3
#define SERIAL_16550         4
#define SERIAL_16650         5
#define SERIAL_16750         6
#define SERIAL_16850         7
#define SERIAL_16950         8

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

/* ------------------------------------------------------------------------ */
/* Backend interface: the seam between a chip and what is on the other side */
/* ------------------------------------------------------------------------ */

/* A backend is what the chip talks to. Every method is optional: a backend
   that has no modem control lines leaves ->status NULL, one that cannot
   receive leaves ->read_byte NULL, and the chip falls back to what it did
   before this interface existed (no polling, no data). */
typedef struct serial_backend_s {
    int      (*read_byte)(void *priv);                 /* next byte, or -1 */
    void     (*write_byte)(void *priv, uint8_t val);   /* byte out of the port */
    uint32_t (*status)(void *priv);                    /* CTS/DSR/RI/DCD bits */
    void     (*control)(void *priv, uint32_t flags);   /* DTR/RTS/loopback */
    void     (*set_format)(void *priv, int baud, int data_bits, int parity, int stop_bits);

    /* Hooks an in-machine device asked for through serial_attach_ex*(). */
    void     (*set_lcr)(void *priv, uint8_t lcr);
    void     (*rcr)(void *priv);
    void     (*dtr)(void *priv, int on);
    void     (*transmit_period)(void *priv, double period);

    void      *priv;
} serial_backend_t;

struct serial_s;
struct serial_device_s;

/* Storage for a backend. Each chip owns one, so two chips on the same port index
   can no longer overwrite each other's adapter, and a closed chip takes its
   adapter with it. The `priv` handed to every method points here. */
typedef struct serial_backend_store_s {
    serial_backend_t        be;
    char_port_t            *port;   /* chardev backend only */
    struct serial_s        *serial; /* device backend only */
    struct serial_device_s *sd;     /* device backend only */
} serial_backend_store_t;

/* Adapter for a host chardev; the storage belongs to the calling chip. */
extern const serial_backend_t *serial_backend_from_chardev(char_port_t *port,
                                                           serial_backend_store_t *store);

/* Adapter for another emulated device (modem, serial mouse, tablet, ...). */
extern const serial_backend_t *serial_backend_from_device(struct serial_s *serial,
                                                          struct serial_device_s *sd,
                                                          serial_backend_store_t *store);

/* ------------------------------------------------------------------------ */
/* In-machine device callbacks                                              */
/* ------------------------------------------------------------------------ */

typedef struct serial_device_s {
    void    (*rcr_callback)(struct serial_s *serial, void *priv);
    void    (*dtr_callback)(struct serial_s *serial, int status, void *priv);
    void    (*dev_write)(struct serial_s *serial, void *priv, uint8_t data);
    void    (*lcr_callback)(struct serial_s *serial, void *priv, uint8_t lcr);
    void    (*transmit_period_callback)(struct serial_s *serial, void *priv, double transmit_period);
    void     *priv;
} serial_device_t;

/* ------------------------------------------------------------------------ */
/* UART state (NS8250 family)                                               */
/* ------------------------------------------------------------------------ */

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

    /* Divides the receive timer down for the modem status poll. */
    uint8_t status_divider;
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

    uint8_t *reg_91;

    void *rcvr_fifo;
    void *xmit_fifo;

    pc_timer_t transmit_timer;
    pc_timer_t timeout_timer;
    pc_timer_t receive_timer;
    double     clock_src;
    double     transmit_period;

    struct serial_device_s *sd;
    /* Whatever is on the other side of the port; NULL when nothing is attached. */
    const serial_backend_t *be;
    /* Storage for `be`; owned by this chip, so it goes away with the chip. */
    serial_backend_store_t  be_store;

} serial_t;

/* ------------------------------------------------------------------------ */
/* Device side interface: what an in-machine device can do to the chip      */
/* ------------------------------------------------------------------------ */

typedef struct serial_port_ops_s {
    void (*receive)(void *chip, uint8_t byte);           /* queue a byte for the guest */
    int  (*can_receive)(void *chip);                     /* room for one more byte? */
    void (*set_line)(void *chip, uint32_t line, int on); /* CTS/DSR/RI/DCD or DTR/RTS */
    int  (*get_line)(void *chip, uint32_t line);
} serial_port_ops_t;

/* ------------------------------------------------------------------------ */
/* Port table (host side policy)                                            */
/* ------------------------------------------------------------------------ */

typedef struct ser_port_s {
    uint8_t enabled;
    uint8_t hotunplug;
    int     device;

    /* Set by every chip that owns this port: the host layer uses them to hand
       the chip a freshly initialised chardev or device backend, or NULL when
       the port is released, and to reach the device side interface. */
    void  *chip;
    void (*set_host_backend)(void *chip, char_port_t *port);
    void (*set_device_backend)(void *chip, struct serial_device_s *sd);
    const serial_port_ops_t *ops;

    /* The host-side backend lives with the port, not with the chip. */
    char_port_t char_port;

    serial_t *serial;
} ser_port_t;

extern ser_port_t com_ports[SERIAL_MAX];

/* Storage for an in-machine device's callbacks; owned by serial_host.c. */
extern void serial_device_attach(int port);

/* ------------------------------------------------------------------------ */
/* Attach API                                                               */
/* ------------------------------------------------------------------------ */

extern ser_port_t *serial_attach_ex(int port,
                                  void (*rcr_callback)(struct serial_s *serial, void *priv),
                                  void (*dev_write)(struct serial_s *serial, void *priv, uint8_t data),
                                  void (*transmit_period_callback)(struct serial_s *serial, void *priv, double transmit_period),
                                  void (*lcr_callback)(struct serial_s *serial, void *priv, uint8_t data_bits),
                                  void *priv);

extern ser_port_t *serial_attach_ex_2(int port,
                                  void (*rcr_callback)(struct serial_s *serial, void *priv),
                                  void (*dev_write)(struct serial_s *serial, void *priv, uint8_t data),
                                  void (*dtr_callback)(struct serial_s *serial, int status, void *priv),
                                  void *priv);

#define serial_attach(port, rcr_callback, dev_write, priv) \
        serial_attach_ex(port, rcr_callback, dev_write, NULL, NULL, priv);

/* Device side: what an in-machine device can do to the chip on its port. */
extern void serial_port_receive(ser_port_t *port, uint8_t byte);
extern int  serial_port_can_receive(ser_port_t *port);
extern void serial_port_set_line(ser_port_t *port, uint32_t line, int on);
extern int  serial_port_get_line(ser_port_t *port, uint32_t line);
extern void serial_port_detach(ser_port_t *port);

/* ------------------------------------------------------------------------ */
/* UART driver API                                                          */
/* ------------------------------------------------------------------------ */

extern void      serial_devices_init(void);
extern void      serial_devices_close(int soft);
extern void      serial_devices_reset(void);
extern void      serial_remove(serial_t *dev);
extern void      serial_setup(serial_t *dev, uint16_t addr, uint8_t irq);
extern void      serial_irq(serial_t *dev, uint8_t irq);
extern void      serial_write_fifo(serial_t *dev, uint8_t dat);
extern void      serial_set_next_inst(int ni);
extern void      serial_standalone_init(void);
extern void      serial_set_clock_src(serial_t *dev, double clock_src);
extern void      serial_set_type(serial_t *dev, uint8_t type);
extern void      serial_set_card_selected_feedback(serial_t *dev, uint8_t *reg_91);
extern void      serial_reset_port(serial_t *dev);
extern uint8_t   serial_read(uint16_t addr, void *priv);
extern void      serial_write(uint16_t addr, uint8_t val, void *priv);
extern void      serial_device_timeout(void *priv);
extern void      serial_set_cts(serial_t *dev, uint8_t enabled);
extern void      serial_set_dsr(serial_t *dev, uint8_t enabled);
extern void      serial_set_dcd(serial_t *dev, uint8_t enabled);
extern void      serial_set_ri(serial_t *dev, uint8_t enabled);
extern int       serial_get_ri(serial_t *dev);
extern uint8_t   serial_get_shadow(serial_t *dev);

/* ------------------------------------------------------------------------ */
/* Intel 8251A                                                              */
/* ------------------------------------------------------------------------ */

extern uint8_t   i8251_read(uint16_t addr, void *priv);
extern void      i8251_write(uint16_t addr, uint8_t val, void *priv);
extern void      i8251_set_irq(void *priv, int irq_mask);
extern void      i8251_set_clock(void *priv, double hz);

/* ------------------------------------------------------------------------ */
/* Device declarations                                                      */
/* ------------------------------------------------------------------------ */

extern const device_t i8251_device;
extern const device_t ns8250_device;
extern const device_t ns8250_pcjr_3f8_device;
extern const device_t ns8250_pcjr_2f8_device;
extern const device_t ns16450_device;
extern const device_t ns16550_device;
extern const device_t ns16650_device;
extern const device_t ns16750_device;
extern const device_t ns16850_device;
extern const device_t ns16950_device;

#endif /*EMU_SERIAL_H*/
