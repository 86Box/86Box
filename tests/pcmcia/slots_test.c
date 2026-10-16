/*
 * 86Box       The PC Card slots (src/pcmcia/pcmcia.c) against fake cards:
 *             hot-plug requests as the PC Card icon and the PCMCIA settings
 *             make them, carried out by the controller's poll -- a card put
 *             in, taken out, swapped for another, and put in again with new
 *             settings (out, the socket empty for a while, then in).
 *
 *             No test framework; returns non-zero on failure.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/scsi.h>
#include <86box/pcmcia.h>
#include <86box/plat_unused.h>

static int checks, failures;
#define CHECK(c, ...)                                     \
    do {                                                  \
        checks++;                                         \
        if (!(c)) {                                       \
            failures++;                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

/* ---- the platform ---------------------------------------------------- */

void pclog_ex(UNUSED(const char *f), UNUSED(va_list a)) { }
void pclog(UNUSED(const char *f), ...) { }

void *
device_add(const device_t *d)
{
    return d->init(d);
}

static int context_inst;
void device_context_inst(UNUSED(const device_t *dev), int inst) { context_inst = inst; }
void device_context_restore(void) { context_inst = 0; }
int  device_has_config(const device_t *dev) { return dev->config != NULL; }
const char *device_get_internal_name(const device_t *dev) { return dev->internal_name; }

void    pcmcia_controller_add(void) { }
uint8_t scsi_get_bus(void) { return 2; }

netcard_conf_t net_cards_conf[NET_CONF_MAX];
uint16_t       net_card_current;
netcard_t *network_attach(UNUSED(void *card_drv), UNUSED(uint8_t *mac), UNUSED(NETRXCB rx), UNUSED(NETSETLINKSTATE s)) { return NULL; }

/* ---- the cards: each open one is a socket's, made with its instance -- */

static int opened[PCMCIA_SOCKETS], closed[PCMCIA_SOCKETS];
static int live[PCMCIA_SOCKETS];   /* the type each socket's open card is, 0: none */

typedef struct fake_t {
    int socket;
    int type;
} fake_t;

static void *
fake_init(const device_t *info)
{
    fake_t *f = calloc(1, sizeof(fake_t));

    f->socket = context_inst - 1;
    f->type   = (int) info->local;
    opened[f->socket]++;
    live[f->socket] = f->type;
    return f;
}

static void
fake_close(void *priv)
{
    fake_t *f = (fake_t *) priv;

    closed[f->socket]++;
    live[f->socket] = 0;
    free(f);
}

#define FAKE(n, t) \
    const device_t n = { .name = #n, .internal_name = #n, .local = t, .init = fake_init, .close = fake_close }

/* In pcmcia.c's list order: 1 3C589D, 2 TE100, 3 APA-1460. */
const device_t device_none = { .name = "None", .internal_name = "none" };
FAKE(threec589d_device, 1);
FAKE(te100pc16_device, 2);
FAKE(apa1460_device, 3);

static void
poll(int n)
{
    while (n--)
        pcmcia_slots_poll();
}

#define SWAP_POLLS 200   /* as pcmcia.c */

int
main(void)
{
    /* A hard reset: a 3C589D in A, B empty. */
    pcmcia_enabled      = 1;
    pcmcia_card_type[0] = 1;
    pcmcia_card_type[1] = 0;
    pcmcia_reset();
    CHECK(pcmcia_slots_active(), "the slots run after the reset");
    CHECK((live[0] == 1) && (opened[0] == 1), "the 3C589D is in A (live %d, opened %d)", live[0], opened[0]);
    CHECK((live[1] == 0) && (opened[1] == 0), "B is empty");

    /* New settings for the card that came in at the reset (never requested
       before): the same card out, then in again -- not swapped for none. */
    pcmcia_request_reinsert(0);
    poll(1);
    CHECK((live[0] == 0) && (closed[0] == 1), "reinsert: the card is out first");
    poll(SWAP_POLLS - 2);
    CHECK(live[0] == 0, "reinsert: the socket is seen empty for a while");
    poll(1);
    CHECK((live[0] == 1) && (opened[0] == 2), "reinsert: the same card back (live %d, opened %d)", live[0], opened[0]);
    CHECK(pcmcia_card_type[0] == 1, "reinsert keeps the setting");
    poll(SWAP_POLLS * 2);
    CHECK((opened[0] == 2) && (closed[0] == 1), "reinsert: once only");

    /* An empty socket: nothing to put in again. */
    pcmcia_request_reinsert(1);
    poll(SWAP_POLLS + 1);
    CHECK((opened[1] == 0) && (live[1] == 0), "reinsert of an empty socket does nothing");

    /* A card into the empty socket: at once. */
    pcmcia_request_card(1, 2);
    poll(1);
    CHECK((live[1] == 2) && (opened[1] == 1), "a card into an empty socket goes in at once");
    CHECK(pcmcia_card_type[1] == 2, "and the setting follows");

    /* Another card for it: out, then the new one in. */
    pcmcia_request_card(1, 1);
    poll(1);
    CHECK((live[1] == 0) && (closed[1] == 1), "swap: the old card out first");
    poll(SWAP_POLLS - 1);
    CHECK((live[1] == 1) && (opened[1] == 2), "swap: then the new card in (live %d)", live[1]);

    /* New settings while a swap is still waiting: the new card goes in
       once, with them. */
    pcmcia_request_card(1, 2);
    poll(1);
    pcmcia_request_reinsert(1);
    poll(SWAP_POLLS);
    CHECK((live[1] == 2) && (opened[1] == 3) && (closed[1] == 2), "reinsert during a swap: one card in (live %d, opened %d, closed %d)",
          live[1], opened[1], closed[1]);

    /* Out. */
    pcmcia_request_card(1, 0);
    poll(1);
    CHECK((live[1] == 0) && (closed[1] == 3), "a card taken out goes at once");
    poll(SWAP_POLLS + 1);
    CHECK((live[1] == 0) && (opened[1] == 3), "and stays out");

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
