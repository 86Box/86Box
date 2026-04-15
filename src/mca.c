/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/mca.h>

#define MCA_MAX_CARDS 8

_Static_assert(MCA_MAX_CARDS > 0 && MCA_MAX_CARDS <= 255, "MCA_MAX_CARDS must be between 1 and 255");

/**
 * @struct mca_slot_t
 * @brief Internal representation of a physical MCA slot and its attached adapter.
 *
 * Pointers are NULL if the slot is unoccupied.
 */
typedef struct mca_slot_s {
    uint8_t (*read)(uint16_t port, void *priv);
    void    (*write)(uint16_t port, uint8_t val, void *priv);
    uint8_t (*feedb)(void *priv);
    void    (*reset)(void *priv);
    void    *priv;
} mca_slot_t;

static mca_slot_t mca_slots[MCA_MAX_CARDS];
static uint8_t    mca_index;
static uint8_t    mca_nr_cards;

#define ENABLE_MCA_LOG 1
#if ENABLE_MCA_LOG
uint8_t mca_do_log = ENABLE_MCA_LOG;

static void
mca_log(const char *fmt, ...)
{
    va_list ap;

    if (mca_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define mca_log(fmt, ...) do { } while (0)
#endif

void
mca_init(const uint8_t nr_cards)
{
    memset(mca_slots, 0, sizeof(mca_slots));

    mca_index    = 0;
    mca_nr_cards = nr_cards;
}

void
mca_set_index(const uint8_t index)
{
    mca_index = index;
}

uint8_t
mca_read(const uint16_t port)
{
    if ((mca_index >= mca_nr_cards) || (!mca_slots[mca_index].read))
        return 0xff;

    return mca_slots[mca_index].read(port, mca_slots[mca_index].priv);
}

uint8_t
mca_read_index(const uint16_t port, const uint8_t index)
{
    if ((index >= mca_nr_cards) || (!mca_slots[index].read))
        return 0xff;

    return mca_slots[index].read(port, mca_slots[index].priv);
}

uint8_t
mca_get_nr_cards(void)
{
    return mca_nr_cards;
}

void
mca_write(const uint16_t port, const uint8_t val)
{
    if ((mca_index >= mca_nr_cards) || (!mca_slots[mca_index].write))
        return;

    mca_slots[mca_index].write(port, val, mca_slots[mca_index].priv);
}

uint8_t
mca_feedb(void)
{
    if ((mca_index >= mca_nr_cards) || (!mca_slots[mca_index].feedb))
        return 0;

    return !!(mca_slots[mca_index].feedb(mca_slots[mca_index].priv));
}

void
mca_reset(void)
{
    for (uint8_t slot = 0; slot < MCA_MAX_CARDS; slot++) {
        if (mca_slots[slot].reset)
            mca_slots[slot].reset(mca_slots[slot].priv);
    }
}

void
mca_add(uint8_t (*read)(uint16_t port, void *priv),
        void (*write)(uint16_t port, uint8_t val, void *priv),
        uint8_t (*feedb)(void *priv),
        void (*reset)(void *priv),
        void *priv)
{
    for (uint8_t slot = 0; slot < mca_nr_cards; slot++) {
        if (!mca_slots[slot].read && !mca_slots[slot].write) {
            mca_slots[slot] = (mca_slot_t) {
                .read  = read,
                .write = write,
                .feedb = feedb,
                .reset = reset,
                .priv  = priv
            };
            return;
        }
    }
}

void
mca_add_to_slot(uint8_t (*read)(uint16_t port, void *priv),
                void (*write)(uint16_t port, uint8_t val, void *priv),
                uint8_t (*feedb)(void *priv),
                void (*reset)(void *priv),
                void *priv,
                uint8_t slot)
{
    if (slot >= MCA_MAX_CARDS)
        return;

    if (mca_slots[slot].read || mca_slots[slot].write) {
        mca_log("MCA: Cannot add device to slot %d - slot occupied\n", slot);
        return;
    }

    mca_slots[slot] = (mca_slot_t) {
        .read  = read,
        .write = write,
        .feedb = feedb,
        .reset = reset,
        .priv  = priv
    };
}
