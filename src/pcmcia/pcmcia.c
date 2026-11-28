/*
 * 86Box       A hypervisor and IBM PC system emulator.
 *
 *             The PCMCIA settings: whether the PC Card controller
 *             (pcic_pd6722.c) is fitted, and the card in each of its two
 *             sockets, with a network card's link.
 *
 *             In 86box.cfg:
 *
 *               [PCMCIA]
 *               controller = 1
 *               socket_a = te100pc16          card internal names
 *               socket_a_net_type = slirp     none, slirp or pcap
 *               socket_a_net_host = ...       the pcap host interface
 *               socket_b = compactflash
 *               socket_b_cf_image = ...      raw CompactFlash image
 *               socket_b_cf_wp = 1           optional write protection
 *
 *             and each card's own settings in its device section, one
 *             instance per socket ("... #1" for A, "#2" for B).
 *
 *             A network card links through the NET_CARD_MAX + socket entry of
 *             net_cards_conf, after the four of the Network settings, so it
 *             takes none of those.
 *
 *             A SCSI card (the APA-1460) has its socket's SCSI bus: the next
 *             free one, kept for the socket from the hard reset that finds a
 *             SCSI card set for it -- after the machine's, the sound cards'
 *             and the SCSI cards' buses, socket A's before B's, as scsi_plan()
 *             shows them -- or from the first time one is put into it, to the
 *             next hard reset.  So the disks on that bus are there whenever
 *             the card is, however often it comes and goes.
 *
 *             PC Cards are hot-pluggable: the PC Card status bar icon puts a
 *             card into a socket or takes it out while the machine runs.  So
 *             the cards are not in the device list -- whose devices live from
 *             one hard reset to the next -- but are made and closed here, each
 *             with its socket's device context (instance socket + 1, so its
 *             own settings), inside the "PC Card slots" device, which closes
 *             them all at a hard reset.  A request from the UI thread is
 *             carried out on the emulation thread by the controller's poll,
 *             and the socket sees the card come or go (pcic_pd6722.c).  One
 *             card for another is two events, as by hand: the old card out,
 *             the socket empty for SWAP_POLLS, then the new one in.  Both at
 *             once are one card detect change with a card still there, which
 *             Windows 9x takes for no removal at all -- the old card's
 *             drivers stay, and it handles no change in that socket again.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/scsi.h>
#include <86box/pcmcia.h>
#include <86box/hdc_ide.h>
#include <86box/plat_unused.h>

extern const device_t te100pc16_device;
extern const device_t threec589d_device;
extern const device_t apa1460_device;
extern const device_t pccard_cf_device;

int  pcmcia_enabled;
int  pcmcia_card_type[PCMCIA_SOCKETS];
int  pcmcia_net_type[PCMCIA_SOCKETS];
char pcmcia_net_host[PCMCIA_SOCKETS][128];

static const struct {
    const device_t *dev;
    int             network;
    int             scsi;    /* a SCSI host adapter: takes its socket's bus */
} cards[] = {
    // clang-format off
    { &device_none,             0, 0 },
    { &threec589d_device,       1, 0 },
    { &te100pc16_device,        1, 0 },
    { &apa1460_device,          0, 1 },
    { &pccard_cf_device,        0, 0 },
    { NULL,                     0, 0 }
    // clang-format on
};

/* The cards in the sockets now: their device, and what its init gave. */
static int           slots_running;
static int           live_type[PCMCIA_SOCKETS];
static void         *live_priv[PCMCIA_SOCKETS];
static int           want_pending[PCMCIA_SOCKETS];
static int           want_again[PCMCIA_SOCKETS];   /* the same card out and in again */
static int           target_type[PCMCIA_SOCKETS];
static int           swap_wait[PCMCIA_SOCKETS];   /* polls until the new card goes in */
static uint8_t       scsi_bus[PCMCIA_SOCKETS];   /* 0xFF: none kept yet */
/* The UI publishes card changes and image settings together. The poll
   snapshots both sockets before handling removals and insertions. */
static mutex_t      *slots_mutex;
static char          cf_image[PCMCIA_SOCKETS][MAX_IMAGE_PATH_LEN];
static int           cf_wp[PCMCIA_SOCKETS];
static char          cf_history[MAX_PREV_IMAGES][MAX_IMAGE_PATH_LEN];
static char          target_image[PCMCIA_SOCKETS][MAX_IMAGE_PATH_LEN];
static int           target_wp[PCMCIA_SOCKETS];

void
pcmcia_cf_config_init(void)
{
    if (!slots_mutex)
        slots_mutex = thread_create_mutex();
    memset(cf_image, 0, sizeof(cf_image));
    memset(cf_wp, 0, sizeof(cf_wp));
    memset(cf_history, 0, sizeof(cf_history));
}

void
pcmcia_cf_set_image(int s, const char *fn, int wp)
{
    thread_wait_mutex(slots_mutex);
    snprintf(cf_image[s], sizeof(cf_image[s]), "%s", fn);
    cf_wp[s] = !!wp;
    thread_release_mutex(slots_mutex);
}

int
pcmcia_cf_get_image(int s, char *fn, int n, int *wp)
{
    thread_wait_mutex(slots_mutex);
    snprintf(fn, n, "%s", cf_image[s]);
    if (wp)
        *wp = cf_wp[s];
    thread_release_mutex(slots_mutex);
    return fn[0] != '\0';
}

const char *
pcmcia_cf_target_image(int s, int *wp)
{
    *wp = target_wp[s];
    return target_image[s];
}

int
pcmcia_cf_history_get(int i, char *fn, int n)
{
    thread_wait_mutex(slots_mutex);
    snprintf(fn, n, "%s", cf_history[i]);
    thread_release_mutex(slots_mutex);
    return fn[0] != '\0';
}

void
pcmcia_cf_history_set(int i, const char *fn)
{
    thread_wait_mutex(slots_mutex);
    snprintf(cf_history[i], sizeof(cf_history[i]), "%s", fn);
    thread_release_mutex(slots_mutex);
}

void
pcmcia_cf_history_clear(void)
{
    thread_wait_mutex(slots_mutex);
    memset(cf_history, 0, sizeof(cf_history));
    thread_release_mutex(slots_mutex);
}

int
pcmcia_cf_insert_image(int s, const char *fn, int wp)
{
    const int type = pcmcia_card_get_from_internal_name(pccard_cf_device.internal_name);
    int       i;

    if ((s < 0) || (s >= PCMCIA_SOCKETS) || !fn[0] || (strlen(fn) >= MAX_IMAGE_PATH_LEN) ||
        !ide_cf_image_valid(fn))
        return 0;

    thread_wait_mutex(slots_mutex);
    for (int other = 0; other < PCMCIA_SOCKETS; other++) {
        if ((other != s) && pcmcia_card_is_cf(pcmcia_card_type[other]) && !strcmp(cf_image[other], fn)) {
            pcmcia_card_type[other] = 0;
            cf_image[other][0]      = '\0';
            cf_wp[other]           = 0;
            want_pending[other]    = 1;
        }
    }
    if ((pcmcia_card_type[s] != type) || strcmp(cf_image[s], fn) || (cf_wp[s] != !!wp)) {
        snprintf(cf_image[s], sizeof(cf_image[s]), "%s", fn);
        cf_wp[s]           = !!wp;
        pcmcia_card_type[s] = type;
        want_again[s]      = 1;
    }
    /* Also retry an unchanged request if the previous open failed. */
    want_pending[s] = 1;
    for (i = 0; i < MAX_PREV_IMAGES - 1; i++)
        if (!strcmp(cf_history[i], fn))
            break;
    memmove(cf_history[1], cf_history[0], (size_t) i * sizeof(cf_history[0]));
    snprintf(cf_history[0], sizeof(cf_history[0]), "%s", fn);
    thread_release_mutex(slots_mutex);
    return 1;
}

static void
card_close(int s)
{
    if (live_type[s] && live_priv[s] && cards[live_type[s]].dev->close)
        cards[live_type[s]].dev->close(live_priv[s]);
    live_type[s] = 0;
    live_priv[s] = NULL;
}

static void
card_open(int s, int type)
{
    card_close(s);
    if ((type <= 0) || (type >= pcmcia_card_count()))
        return;
    device_context_inst(cards[type].dev, s + 1);
    live_priv[s] = cards[type].dev->init(cards[type].dev);
    device_context_restore();
    live_type[s] = live_priv[s] ? type : 0;
    if (live_priv[s])
        pclog("PCMCIA: socket %c: %s\n", 'A' + s, cards[type].dev->name);
}

static void *
slots_init(UNUSED(const device_t *info))
{
    slots_running = 1;
    for (int s = 0; s < PCMCIA_SOCKETS; s++) {
        scsi_bus[s] = 0xff;
        if (pcmcia_card_is_scsi(pcmcia_card_type[s]))
            pcmcia_scsi_bus(s);
    }
    for (int s = 0; s < PCMCIA_SOCKETS; s++) {
        want_pending[s] = 0;
        target_type[s]  = pcmcia_card_type[s];
        pcmcia_cf_get_image(s, target_image[s], sizeof(target_image[s]), &target_wp[s]);
        want_again[s]   = 0;
        swap_wait[s]    = 0;
        card_open(s, pcmcia_card_type[s]);
    }
    return &slots_running;
}

static void
slots_close(UNUSED(void *priv))
{
    for (int s = 0; s < PCMCIA_SOCKETS; s++)
        card_close(s);
    slots_running = 0;
}

static const device_t pcmcia_slots_device = {
    .name          = "PC Card slots",
    .internal_name = "pcmcia_slots",
    .flags         = 0,
    .local         = 0,
    .init          = slots_init,
    .close         = slots_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

void
pcmcia_reset(void)
{
    if (!pcmcia_enabled)
        return;

    pcmcia_controller_add();
    device_add(&pcmcia_slots_device);
}

int
pcmcia_slots_active(void)
{
    return slots_running;
}

/* From the UI: put a card of this type (0: none) into socket s.  The setting
   follows, so the card is there after a restart too. */
void
pcmcia_request_card(int s, int type)
{
    if ((s < 0) || (s >= PCMCIA_SOCKETS) || (type < 0) || (type >= pcmcia_card_count()))
        return;
    thread_wait_mutex(slots_mutex);
    if (!pcmcia_card_is_cf(type)) {
        cf_image[s][0] = '\0';
        cf_wp[s]       = 0;
    }
    pcmcia_card_type[s] = type;
    want_pending[s]     = 1;
    thread_release_mutex(slots_mutex);
}

/* From the UI: the card in socket s with new settings (its own, or its
   network link) -- taken out and put back, so it comes up with them, as a
   card taken out and put in again by hand.  An empty socket: nothing. */
void
pcmcia_request_reinsert(int s)
{
    if ((s < 0) || (s >= PCMCIA_SOCKETS))
        return;
    thread_wait_mutex(slots_mutex);
    want_again[s]   = 1;
    want_pending[s] = 1;
    thread_release_mutex(slots_mutex);
}

/* On the emulation thread (the controller's poll, every 10 ms): carry the
   requests out.  A card for another is the old one out now, the new one in
   after the socket has been seen empty. */
#define SWAP_POLLS 200   /* 2 s: the guest's card services see the socket empty */

void
pcmcia_slots_poll(void)
{
    int change[PCMCIA_SOCKETS] = { 0 };

    if (!slots_running)
        return;
    thread_wait_mutex(slots_mutex);
    for (int s = 0; s < PCMCIA_SOCKETS; s++) {
        if (want_pending[s]) {
            target_type[s]  = pcmcia_card_type[s];
            memcpy(target_image[s], cf_image[s], sizeof(target_image[s]));
            target_wp[s]   = cf_wp[s];
            change[s]      = (target_type[s] != live_type[s]) || (want_again[s] && live_type[s]);
            want_pending[s] = 0;
            want_again[s]   = 0;
        }
    }
    thread_release_mutex(slots_mutex);

    /* Close both sockets' outgoing cards before opening either incoming one. */
    for (int s = 0; s < PCMCIA_SOCKETS; s++) {
        if (change[s] && live_type[s]) {
            card_close(s);
            if (target_type[s])
                swap_wait[s] = SWAP_POLLS;
        }
    }
    for (int s = 0; s < PCMCIA_SOCKETS; s++) {
        /* Whatever was asked for meanwhile -- another card, or none -- goes in. */
        if (swap_wait[s] ? !--swap_wait[s] : change[s])
            card_open(s, target_type[s]);
    }
}

int
pcmcia_card_count(void)
{
    int n = 0;

    while (cards[n].dev)
        n++;
    return n;
}

const char *
pcmcia_card_get_internal_name(int card)
{
    return device_get_internal_name(cards[card].dev);
}

const char *
pcmcia_card_get_name(int card)
{
    return cards[card].dev->name;
}

int
pcmcia_card_get_from_internal_name(const char *s)
{
    for (int c = 0; cards[c].dev; c++)
        if (!strcmp(cards[c].dev->internal_name, s))
            return c;
    return 0;
}

int
pcmcia_card_has_config(int card)
{
    return (card > 0) && (card < pcmcia_card_count()) && device_has_config(cards[card].dev);
}

int
pcmcia_card_is_network(int card)
{
    return (card > 0) && (card < pcmcia_card_count()) && cards[card].network;
}

int
pcmcia_card_is_scsi(int card)
{
    return (card > 0) && (card < pcmcia_card_count()) && cards[card].scsi;
}

int
pcmcia_card_is_cf(int card)
{
    return (card > 0) && (card < pcmcia_card_count()) && (cards[card].dev == &pccard_cf_device);
}

/* The socket's SCSI bus, taken now if it has none yet; 0xFF: none left. */
uint8_t
pcmcia_scsi_bus(int socket)
{
    if ((socket < 0) || (socket >= PCMCIA_SOCKETS))
        return 0xff;
    if (scsi_bus[socket] == 0xff)
        scsi_bus[socket] = scsi_get_bus();
    return scsi_bus[socket];
}

const device_t *
pcmcia_card_get_device(int card)
{
    return cards[card].dev;
}

netcard_t *
pcmcia_network_attach(int socket, void *card_drv, uint8_t *mac, NETRXCB rx, NETSETLINKSTATE set_link_state)
{
    const int       slot  = NET_CARD_MAX + socket;
    netcard_conf_t *conf  = &net_cards_conf[slot];
    const uint16_t  saved = net_card_current;
    const uint32_t  link  = conf->link_state;   /* the status bar's Connected */
    netcard_t      *card;

    memset(conf, 0, sizeof(netcard_conf_t));
    conf->link_state = link;
    conf->net_type   = pcmcia_net_type[socket];
    snprintf(conf->host_dev_name, sizeof(conf->host_dev_name), "%s", pcmcia_net_host[socket]);

    net_card_current = slot;
    card             = network_attach(card_drv, mac, rx, set_link_state);
    net_card_current = saved;

    /* A link that failed to come up was turned into none: so be it in the
       settings too, as for the adapters of the Network settings. */
    pcmcia_net_type[socket] = conf->net_type;
    return card;
}
