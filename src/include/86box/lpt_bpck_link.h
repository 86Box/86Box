/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The MicroSolutions BackPack wire protocol, shared by the
 *          BackPack CD-ROM and hard disk pods and the Iomega Ditto.
 *
 * Authors: Dmitry Brant, <me@dmitrybrant.com>
 *          Mike Lycett
 *          Mario Fetka
 *
 *          Copyright 2026 Dmitry Brant.
 *          Copyright 2026 Mike Lycett.
 *          Copyright 2026 Mario Fetka.
 */
#ifndef EMU_LPT_BPCK_LINK_H
#define EMU_LPT_BPCK_LINK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
   Transfer protocols, in the host drivers' order: below EPP-8 everything
   goes through the SPP registers, above it through base+3 and base+4.
 */
enum {
    BPCK_PROTO_SPP = 0,
    BPCK_PROTO_PS2,
    BPCK_PROTO_EPP8,
    BPCK_PROTO_EPP16,
    BPCK_PROTO_EPP32
};

extern const char *bpck_proto_name[];

/*
   What a pod puts behind the wire. read_reg and write_reg are required;
   the others are optional hooks for what a pod does on top.
 */
typedef struct bpck_link_ops_t {
    uint8_t (*read_reg)(void *priv, int reg);
    void (*write_reg)(void *priv, int reg, uint8_t val);

    /* The host touched the port; status is set for a status read. */
    void (*activity)(void *priv, int status);
    /* The link is up, before its answer goes onto the status lines. */
    void (*connect)(void *priv);
    /* The link was let go. */
    void (*disconnect)(void *priv);
    /* A register was addressed. */
    void (*address)(void *priv);
    /* The host wrote the data lines; old is what they carried before. */
    void (*data)(void *priv, uint8_t val, uint8_t old);
    /* A control line change on a connected link, ahead of decoding it. */
    void (*ctrl)(void *priv, uint8_t chg, uint8_t lines);
} bpck_link_ops_t;

typedef struct bpck_link_t {
    void                  *lpt;
    const bpck_link_ops_t *ops;
    void                  *priv;
    const char            *name; /* the log prefix: "BPCK", "Ditto" */
    void (*log)(const char *fmt, ...);

    /* Which pod on the chain this is; the knock carries the address. */
    uint8_t unit;
    int     proto;     /* protocol the bridge is programmed for */
    int     max_proto; /* fastest protocol the bridge will admit to */

    /* The port as the host last left it. */
    uint8_t ctrl;
    uint8_t dat;
    uint8_t dat_out;

    int connected;
    int knock;         /* SELECT edges seen since it began */
    int ident;         /* answering the post-knock address probe */
    int latching;      /* an out-of-band register write is under way */
    int latch_commits; /* bytes it has committed so far */

    /* The byte being handed back to the host. */
    int     cur_reg;     /* register the host has addressed */
    uint8_t rd_val;      /* byte being shifted out */
    int     rd_high;     /* the low nibble has gone, the high one is next */
    uint8_t rd_out;      /* status bits presenting the current nibble */
    uint8_t last_status; /* last value handed back, to quieten poll loops */
} bpck_link_t;

/*
   The 93C46 a pod identifies itself from, where one is fitted: register
   0x06 carries its lines and register 0x00 bit 7 is DO. Addressing,
   opcode and word size are the standard part; only the contents are the
   pod's to supply.
 */
#define BPCK_EE_ENABLE 0x08
#define BPCK_EE_CS     0x04
#define BPCK_EE_DI     0x02
#define BPCK_EE_CLK    0x01

typedef struct bpck_ee_t {
    uint8_t  lines;
    uint16_t shift;   /* command being clocked in */
    int      bits;    /* bits clocked since CS rose */
    uint16_t out;     /* word being clocked out */
    int      reading;
    int      dout;
    uint16_t data[64];
} bpck_ee_t;

/* The lines the host writes to register 0x06. */
extern void bpck_ee_write(bpck_ee_t *ee, uint8_t val, const char *name,
                          void (*log)(const char *fmt, ...));

/* Sets the link up and claims the parallel port of the device instance;
   0 if another device already has it. */
extern int bpck_link_attach(bpck_link_t *link, const bpck_link_ops_t *ops, void *priv,
                            const char *name, void (*log)(const char *fmt, ...));

/* Whether the link is up in a protocol the bridge will talk. */
extern int bpck_link_usable(const bpck_link_t *link);

/* The link's state for a trace line. */
extern const char *bpck_link_state(const bpck_link_t *link);

#ifdef __cplusplus
}
#endif

#endif /*EMU_LPT_BPCK_LINK_H*/
