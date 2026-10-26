/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The EISA bus: slot specific I/O and the product identifier
 *          every card answers with.
 *
 * Authors: Michael Pratte, <mpratte@makefox.group>
 *
 *          Copyright 2026 Michael Pratte.
 */
#ifndef EMU_EISA_H
#define EMU_EISA_H

/* Slots are numbered from one; slot zero is the system board itself. */
#define EISA_MAX_SLOTS 15

/* A slot's own I/O is the sixteen kilobytes at slot << 12, of which only
   the four ranges with address bits 9:8 clear belong to the card. The
   product identifier sits at the top of the last of them. */
#define EISA_SLOT_ADDR(slot)  ((uint16_t) ((slot) << 12))
#define EISA_SLOT_OF(port)    ((uint8_t) ((port) >> 12))
#define EISA_SLOT_SPECIFIC(p) (((p) & 0x0300) == 0x0000)
#define EISA_ID_OFFSET        0x0c80

/* What a card is asked. Ports are the full sixteen bit address, so a card
   that wants to know its own offset should mask with 0x0fff. */
typedef struct eisa_slot_t {
    uint8_t (*read)(uint16_t port, void *priv);
    void (*write)(uint16_t port, uint8_t val, void *priv);
    /* Optional. A card that answers word and dword cycles itself sets
       these; one that does not is read and written a byte at a time,
       low byte first, which is what the bus would do to it. */
    uint16_t (*readw)(uint16_t port, void *priv);
    void (*writew)(uint16_t port, uint16_t val, void *priv);
    uint32_t (*readl)(uint16_t port, void *priv);
    void (*writel)(uint16_t port, uint32_t val, void *priv);
    void (*reset)(void *priv);
    void   *priv;
    uint8_t id[4]; /* the compressed product identifier, as read at zC80 */
    uint8_t present;
} eisa_slot_t;

/* Turn "ADP" and 0x0400 into the four bytes a card answers with. The
   manufacturer is three letters packed five bits each, the product two
   bytes of its own. */
extern void    eisa_make_id(uint8_t *id, const char *mfg, uint16_t product,
                            uint8_t revision);
extern void    eisa_init(uint8_t nr_slots);
extern void    eisa_close(void);
extern uint8_t eisa_add(uint8_t slot, const uint8_t *id,
                        uint8_t (*read)(uint16_t port, void *priv),
                        void (*write)(uint16_t port, uint8_t val, void *priv),
                        void (*reset)(void *priv), void *priv);
extern void    eisa_remove(uint8_t slot);
extern void    eisa_set_wide(uint8_t slot,
                             uint16_t (*readw)(uint16_t port, void *priv),
                             void (*writew)(uint16_t port, uint16_t val, void *priv),
                             uint32_t (*readl)(uint16_t port, void *priv),
                             void (*writel)(uint16_t port, uint32_t val, void *priv));
extern void    eisa_reset(void);
extern uint8_t eisa_slots_present(void);
extern uint8_t eisa_slot_id(uint8_t slot, uint8_t byte);
extern uint8_t eisa_slot_occupied(uint8_t slot);

/* The system board is slot zero and its identifier lives in the ESC, not
   here; this is how the ESC hands it over. */
extern void eisa_set_board_id(const uint8_t *id);

#endif /*EMU_EISA_H*/
