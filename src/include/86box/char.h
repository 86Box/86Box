/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for character devices.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2025 RichardG.
 */
#ifndef EMU_CHAR_H
#define EMU_CHAR_H

enum {
    CHAR_LPT_USESTROBE = 0x1
};

enum {
    CHAR_PORT_COM = 0x1,
    CHAR_PORT_LPT = 0x2
};

enum {
    CHAR_COM_PARITY_ODD   = 0x1,
    CHAR_COM_PARITY_EVEN  = 0x3,
    CHAR_COM_PARITY_MARK  = 0x5,
    CHAR_COM_PARITY_SPACE = 0x7
};

enum { /* device control */
    /* COM */
    CHAR_COM_DTR = 0x1,
    CHAR_COM_RTS = 0x2,
    CHAR_COM_BREAK = 0x40,

    /* LPT */
    CHAR_LPT_STROBE = 0x100,
    CHAR_LPT_LINEFEED = 0x200,
    CHAR_LPT_RESET = 0x400,
    CHAR_LPT_PSELECT = 0x800,
};

enum { /* device status */
    /* COM */
    CHAR_COM_CTS = 0x10,
    CHAR_COM_DSR = 0x20,
    CHAR_COM_RI  = 0x40,
    CHAR_COM_DCD = 0x80,

    /* LPT */
    CHAR_LPT_ERROR = 0x800,
    CHAR_LPT_SELECT = 0x1000,
    CHAR_LPT_PAPEROUT = 0x2000,
    CHAR_LPT_ACK = 0x4000,
    CHAR_LPT_BUSY = 0x8000,

    /* global status */
    CHAR_DISCONNECTED = 0x80000000
};

typedef struct {
    struct _char_device_ *chardev;
    void *priv;

    uint8_t type;
    union {
        struct {
            uint32_t baud;
            uint8_t  data_bits;
            uint8_t  parity;
            uint8_t  stop_bits;
        } com;
        struct {
            uint8_t  dummy;
        } lpt;
    };
} char_port_t;

typedef struct _char_device_ {
    const device_t device;

    uint32_t flags;

    ssize_t       (*read)(uint8_t *buf, ssize_t len, void *priv);
    ssize_t       (*write)(uint8_t *buf, ssize_t len, void *priv);
    void          (*port_config)(void *priv);
    void          (*control)(uint32_t flags, void *priv);
    uint32_t      (*status)(void *priv);
} char_device_t;

extern char_port_t *char_port;

extern const char_device_t hostser_device;

#endif /*EMU_CHAR_H*/
