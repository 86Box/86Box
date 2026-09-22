/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The EISA bus.
 *
 *          What distinguishes EISA from the ISA bus underneath it, as far
 *          as anything here is concerned, is that a card can be found
 *          without being probed for. Every slot answers at an address of
 *          its own, and the first four bytes there are a product
 *          identifier the card cannot be configured out of. Firmware
 *          walks the slots, reads the identifiers, and knows what is in
 *          the machine before a single driver has run.
 *
 *          A slot owns the sixteen kilobytes of I/O at its slot number
 *          shifted up twelve places, but only the four ranges whose
 *          address bits 9:8 are clear: everything else in that window is
 *          an alias of the system board and belongs to whatever answers
 *          there instead. That is the AEN decode the ESC does with its
 *          slot specific address enables, and it is why a card may use
 *          z000-z0FF, z400-z4FF, z800-z8FF and zC00-zCFF and nothing
 *          more.
 *
 * Authors: Michael Pratte, <mpratte@makefox.group>
 *
 *          Copyright 2026 Michael Pratte.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/eisa.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_EISA_LOG
int eisa_do_log = ENABLE_EISA_LOG;

static void
eisa_log(const char *fmt, ...)
{
    va_list ap;

    if (eisa_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define eisa_log(fmt, ...)
#endif

static eisa_slot_t eisa_slots[EISA_MAX_SLOTS + 1];
static uint8_t     eisa_nr_slots = 0;

/* The identifier is not ASCII. Three letters, A to Z, are packed into two
   bytes at five bits each with the top bit clear, most significant letter
   first; then two bytes of product number and revision, which are read
   big endian although everything else on the bus is not. */
void
eisa_make_id(uint8_t *id, const char *mfg, uint16_t product, uint8_t revision)
{
    uint16_t packed;

    packed = (uint16_t) (((mfg[0] - '@') & 0x1f) << 10);
    packed |= (uint16_t) (((mfg[1] - '@') & 0x1f) << 5);
    packed |= (uint16_t) ((mfg[2] - '@') & 0x1f);

    id[0] = (uint8_t) (packed >> 8);
    id[1] = (uint8_t) (packed & 0xff);
    id[2] = (uint8_t) (product >> 8);
    /* Three hex digits of product and one of revision, but plenty of IDs
       are written as a plain four digit number ("ADP0002"), so a revision
       of zero leaves the low digit of the product alone. */
    id[3] = (uint8_t) (product & 0xff);
    if (revision & 0x0f)
        id[3] = (uint8_t) ((product & 0xf0) | (revision & 0x0f));
}

/* Every slot is decoded here and handed on, so that a slot nothing lives
   in still answers -- with 0xff, which is what an empty one does and what
   firmware takes as "no card". */
static uint8_t
eisa_read(uint16_t port, UNUSED(void *priv))
{
    uint8_t slot = EISA_SLOT_OF(port);

    if ((slot > eisa_nr_slots) || !EISA_SLOT_SPECIFIC(port))
        return 0xff;

    /* The identifier answers whether or not the card cares to, because on
       real hardware it is not the card answering: it is four bytes the
       slot is required to present. */
    if (((port & 0x0fff) >= EISA_ID_OFFSET) && ((port & 0x0fff) <= (EISA_ID_OFFSET + 3))) {
        if (!eisa_slots[slot].present)
            return 0xff;
        return eisa_slots[slot].id[(port & 3)];
    }

    if (!eisa_slots[slot].present || (eisa_slots[slot].read == NULL))
        return 0xff;

    return eisa_slots[slot].read(port, eisa_slots[slot].priv);
}

static void
eisa_write(uint16_t port, uint8_t val, UNUSED(void *priv))
{
    uint8_t slot = EISA_SLOT_OF(port);

    if ((slot > eisa_nr_slots) || !EISA_SLOT_SPECIFIC(port))
        return;
    if (!eisa_slots[slot].present || (eisa_slots[slot].write == NULL))
        return;

    eisa_slots[slot].write(port, val, eisa_slots[slot].priv);
}

/* The four windows a slot owns, claimed for every slot the board has so
   that an empty one reads 0xff rather than the bus's floating 0xff, which
   looks the same but is not guaranteed. */
/* Word and dword cycles. A card that decodes them itself gets them whole;
   otherwise they are the bytes in order, which is also what the four
   identifier bytes are. */
static uint16_t
eisa_readw(uint16_t port, void *priv)
{
    uint8_t slot = EISA_SLOT_OF(port);

    if ((slot <= eisa_nr_slots) && EISA_SLOT_SPECIFIC(port) && eisa_slots[slot].present && (eisa_slots[slot].readw != NULL) && (((port & 0x0fff) < EISA_ID_OFFSET) || ((port & 0x0fff) > (EISA_ID_OFFSET + 3))))
        return eisa_slots[slot].readw(port, eisa_slots[slot].priv);

    return (uint16_t) (eisa_read(port, priv) | (eisa_read((uint16_t) (port + 1), priv) << 8));
}

static uint32_t
eisa_readl(uint16_t port, void *priv)
{
    uint8_t slot = EISA_SLOT_OF(port);

    if ((slot <= eisa_nr_slots) && EISA_SLOT_SPECIFIC(port) && eisa_slots[slot].present && (eisa_slots[slot].readl != NULL) && (((port & 0x0fff) < EISA_ID_OFFSET) || ((port & 0x0fff) > (EISA_ID_OFFSET + 3))))
        return eisa_slots[slot].readl(port, eisa_slots[slot].priv);

    return (uint32_t) eisa_readw(port, priv) | ((uint32_t) eisa_readw((uint16_t) (port + 2), priv) << 16);
}

static void
eisa_writew(uint16_t port, uint16_t val, void *priv)
{
    uint8_t slot = EISA_SLOT_OF(port);

    if ((slot <= eisa_nr_slots) && EISA_SLOT_SPECIFIC(port) && eisa_slots[slot].present && (eisa_slots[slot].writew != NULL)) {
        eisa_slots[slot].writew(port, val, eisa_slots[slot].priv);
        return;
    }
    eisa_write(port, (uint8_t) val, priv);
    eisa_write((uint16_t) (port + 1), (uint8_t) (val >> 8), priv);
}

static void
eisa_writel(uint16_t port, uint32_t val, void *priv)
{
    uint8_t slot = EISA_SLOT_OF(port);

    if ((slot <= eisa_nr_slots) && EISA_SLOT_SPECIFIC(port) && eisa_slots[slot].present && (eisa_slots[slot].writel != NULL)) {
        eisa_slots[slot].writel(port, val, eisa_slots[slot].priv);
        return;
    }
    eisa_writew(port, (uint16_t) val, priv);
    eisa_writew((uint16_t) (port + 2), (uint16_t) (val >> 16), priv);
}

static void
eisa_claim(uint8_t slot, int set)
{
    static const uint16_t window[4] = { 0x0000, 0x0400, 0x0800, 0x0c00 };
    uint16_t              base      = EISA_SLOT_ADDR(slot);

    for (uint8_t i = 0; i < 4; i++) {
        if (set)
            io_sethandler(base + window[i], 0x0100, eisa_read, eisa_readw, eisa_readl,
                          eisa_write, eisa_writew, eisa_writel, NULL);
        else
            io_removehandler(base + window[i], 0x0100, eisa_read, eisa_readw, eisa_readl,
                             eisa_write, eisa_writew, eisa_writel, NULL);
    }
}

void
eisa_init(uint8_t nr_slots)
{
    if (nr_slots > EISA_MAX_SLOTS)
        nr_slots = EISA_MAX_SLOTS;

    eisa_close();

    memset(eisa_slots, 0, sizeof(eisa_slots));
    eisa_nr_slots = nr_slots;

    for (uint8_t slot = 1; slot <= nr_slots; slot++)
        eisa_claim(slot, 1);

    eisa_log("EISA: %i slots\n", nr_slots);
}

void
eisa_close(void)
{
    for (uint8_t slot = 1; slot <= eisa_nr_slots; slot++)
        eisa_claim(slot, 0);

    memset(eisa_slots, 0, sizeof(eisa_slots));
    eisa_nr_slots = 0;
}

uint8_t
eisa_add(uint8_t slot, const uint8_t *id,
         uint8_t (*read)(uint16_t port, void *priv),
         void (*write)(uint16_t port, uint8_t val, void *priv),
         void (*reset)(void *priv), void *priv)
{
    if ((slot < 1) || (slot > eisa_nr_slots)) {
        eisa_log("EISA: slot %i is not on this board\n", slot);
        return 0;
    }
    if (eisa_slots[slot].present) {
        eisa_log("EISA: slot %i is taken\n", slot);
        return 0;
    }

    memcpy(eisa_slots[slot].id, id, 4);
    eisa_slots[slot].read    = read;
    eisa_slots[slot].write   = write;
    eisa_slots[slot].reset   = reset;
    eisa_slots[slot].priv    = priv;
    eisa_slots[slot].present = 1;

    eisa_log("EISA: slot %i = %02x%02x%02x%02x\n", slot, id[0], id[1], id[2],
             id[3]);
    return 1;
}

void
eisa_set_wide(uint8_t slot,
              uint16_t (*readw)(uint16_t port, void *priv),
              void (*writew)(uint16_t port, uint16_t val, void *priv),
              uint32_t (*readl)(uint16_t port, void *priv),
              void (*writel)(uint16_t port, uint32_t val, void *priv))
{
    if ((slot < 1) || (slot > eisa_nr_slots) || !eisa_slots[slot].present)
        return;
    eisa_slots[slot].readw  = readw;
    eisa_slots[slot].writew = writew;
    eisa_slots[slot].readl  = readl;
    eisa_slots[slot].writel = writel;
}

void
eisa_remove(uint8_t slot)
{
    if ((slot < 1) || (slot > eisa_nr_slots))
        return;

    memset(&eisa_slots[slot], 0, sizeof(eisa_slot_t));
}

void
eisa_reset(void)
{
    for (uint8_t slot = 1; slot <= eisa_nr_slots; slot++) {
        if (eisa_slots[slot].present && (eisa_slots[slot].reset != NULL))
            eisa_slots[slot].reset(eisa_slots[slot].priv);
    }
}

uint8_t
eisa_slots_present(void)
{
    return eisa_nr_slots;
}

uint8_t
eisa_slot_occupied(uint8_t slot)
{
    if ((slot < 1) || (slot > eisa_nr_slots))
        return 0;
    return eisa_slots[slot].present;
}

/* Slot zero is the system board. The ESC answers for it at 0C80-0C83, not
   this file, but the identifier is kept here too so that anything walking
   the slots sees a consistent machine. */
void
eisa_set_board_id(const uint8_t *id)
{
    memcpy(eisa_slots[0].id, id, 4);
    eisa_slots[0].present = 1;
}

uint8_t
eisa_slot_id(uint8_t slot, uint8_t byte)
{
    if ((slot > eisa_nr_slots) || !eisa_slots[slot].present)
        return 0xff;
    return eisa_slots[slot].id[byte & 3];
}
