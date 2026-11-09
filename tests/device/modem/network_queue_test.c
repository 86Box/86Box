#include "../../src/network/network.c"
#include "../../src/network/net_slirp.c"

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            exit(1);                                                        \
        }                                                                   \
    } while (0)

static unsigned received, notified, overflow_logs;
static double   scheduled_period;

int
thread_wait_mutex(mutex_t *mutex)
{
    return 0;
}

int
thread_release_mutex(mutex_t *mutex)
{
    return 0;
}

void
pclog(const char *fmt, ...)
{
    overflow_logs++;
}

void
ui_sb_update_icon(int tag, int active)
{
}

void
ui_sb_update_icon_write(int tag, int write)
{
}

void
timer_on_auto(pc_timer_t *timer, double period)
{
    scheduled_period = period;
}

static int
receive_packet(void *priv, uint8_t *data, int len)
{
    received++;
    return len;
}

static void
notify_backend(void *priv)
{
    notified++;
}

static void
enqueue(netqueue_t *queue, uint32_t sequence)
{
    uint8_t packet[1514] = { 0 };
    memcpy(packet, &sequence, sizeof(sequence));
    CHECK(network_queue_put(queue, packet, sizeof(packet)));
}

static void
expect_packet(netqueue_t *queue, netpkt_t *out, uint32_t sequence)
{
    uint32_t actual;
    CHECK(network_queue_get_swap(queue, out));
    CHECK(out->len == 1514);
    memcpy(&actual, out->data, sizeof(actual));
    CHECK(actual == sequence);
}

int
main(void)
{
    netcard_t *card = calloc(1, sizeof(*card));
    CHECK(card);
    for (unsigned i = 0; i < NET_QUEUE_COUNT; i++)
        network_queue_init(&card->queues[i]);
    card->queued_pkt.data    = calloc(1, NET_MAX_FRAME);
    card->rx                 = receive_packet;
    card->host_drv.notify_in = notify_backend;
    card->byte_period        = NET_PERIOD_100M;
    netqueue_t *rx           = &card->queues[NET_QUEUE_RX];
    netqueue_t *deferred     = &card->queues[NET_QUEUE_RX_ON_TX];
    netqueue_t *tx           = &card->queues[NET_QUEUE_TX_VM];
    netqueue_t *host_tx      = &card->queues[NET_QUEUE_TX_HOST];
    netpkt_t    out          = { .data = calloc(1, NET_MAX_FRAME) };
    CHECK(out.data);

    /* Capacity and wrapping must not depend on the transfer batch size. */
    for (unsigned round = 0; round < 3; round++) {
        for (unsigned i = 0; i < NET_QUEUE_CAPACITY - 1; i++)
            enqueue(rx, round * NET_QUEUE_CAPACITY + i);
        uint8_t packet[1514] = { 0 };
        CHECK(!network_queue_put(rx, packet, sizeof(packet)));
        CHECK(rx->dropped_full == round + 1);
        for (unsigned i = 0; i < NET_QUEUE_CAPACITY - 1; i++)
            expect_packet(rx, &out, round * NET_QUEUE_CAPACITY + i);
        CHECK(network_queue_empty(rx));
    }
    CHECK(overflow_logs == 2);
    CHECK(!network_queue_put(rx, out.data, -1));
    CHECK(rx->dropped_full == 3);

    /* A failed swap leaves ownership and contents with the caller. */
    for (unsigned i = 0; i < NET_QUEUE_CAPACITY - 1; i++)
        enqueue(rx, i);
    uint8_t *original = out.data;
    CHECK(!network_queue_put_swap(rx, &out));
    CHECK(out.data == original);
    CHECK(out.len == 1514);
    for (unsigned i = 0; i < NET_QUEUE_CAPACITY - 1; i++)
        expect_packet(rx, &out, i);

    net_slirp_t slirp = { 0 };
    slirp.card        = card;
    slirp.pkt.data    = calloc(1, NET_MAX_FRAME);
    CHECK(slirp.pkt.data);
    for (unsigned i = 0; i < SLIRP_PKT_BATCH; i++) {
        slirp.pkt_tx_v[i].data = calloc(1, NET_MAX_FRAME);
        CHECK(slirp.pkt_tx_v[i].data);
    }

    /* A 32-packet burst is queued without loss. */
    for (unsigned i = 0; i < 32; i++) {
        uint8_t packet[1514] = { 0 };
        memcpy(packet, &i, sizeof(i));
        CHECK(net_slirp_send_packet(packet, sizeof(packet), &slirp) == sizeof(packet));
    }
    for (unsigned i = 0; i < 32; i++)
        expect_packet(rx, &out, i);
    uint64_t drops_before = rx->dropped_full;

    /* Partial merges retain the tail and new packets cannot overtake it. */
    for (unsigned i = 0; i < NET_QUEUE_CAPACITY - 6; i++)
        enqueue(rx, i);
    for (unsigned i = 0; i < 10; i++)
        enqueue(deferred, 1000 + i);
    slirp.recv_on_tx = 1;
    net_slirp_rx_deferred_packets(&slirp);
    CHECK(slirp.recv_on_tx);
    CHECK(card->rx_deferred_pending);
    CHECK(((deferred->head - deferred->tail) & NET_QUEUE_MASK) == 5);
    CHECK(rx->dropped_full == drops_before);
    CHECK(deferred->dropped_full == 0);
    uint8_t  later[1514] = { 0 };
    uint32_t sequence    = 1010;
    memcpy(later, &sequence, sizeof(sequence));
    CHECK(net_slirp_send_packet(later, sizeof(later), &slirp) == sizeof(later));
    for (unsigned i = 0; i < NET_QUEUE_CAPACITY - 6; i++)
        expect_packet(rx, &out, i);
    for (unsigned i = 0; i < 5; i++)
        expect_packet(rx, &out, 1000 + i);
    net_slirp_rx_deferred_packets(&slirp);
    CHECK(!slirp.recv_on_tx);
    CHECK(!card->rx_deferred_pending);
    for (unsigned i = 5; i <= 10; i++)
        expect_packet(rx, &out, 1000 + i);
    CHECK(network_queue_empty(rx));
    CHECK(network_queue_empty(deferred));

    /* ACK-driven bursts fit without increasing delivery batches. */
    slirp.during_tx = 1;
    for (unsigned i = 0; i < 32; i++) {
        uint8_t packet[1514] = { 0 };
        memcpy(packet, &i, sizeof(i));
        CHECK(net_slirp_send_packet(packet, sizeof(packet), &slirp) == sizeof(packet));
    }
    slirp.during_tx = 0;
    net_slirp_rx_deferred_packets(&slirp);
    CHECK(!slirp.recv_on_tx);
    CHECK(deferred->dropped_full == 0);
    for (unsigned i = 0; i < 32; i++)
        expect_packet(rx, &out, i);

    /* Disconnecting still discards deferred packets instead of delivering them. */
    enqueue(deferred, 1);
    slirp.recv_on_tx = 1;
    net_cards_conf[card->card_num].link_state |= NET_LINK_DOWN;
    net_slirp_rx_deferred_packets(&slirp);
    CHECK(network_queue_empty(deferred));
    CHECK(network_queue_empty(rx));
    CHECK(!slirp.recv_on_tx);
    CHECK(!card->rx_deferred_pending);
    net_cards_conf[card->card_num].link_state &= ~NET_LINK_DOWN;

    for (unsigned i = 0; i < 32; i++) {
        enqueue(rx, i);
        enqueue(tx, i);
    }
    network_rx_queue(card);
    CHECK(received == NET_QUEUE_LEN);
    CHECK(notified == 1);
    CHECK(scheduled_period == NET_PERIOD_100M * NET_QUEUE_LEN * 1514);
    CHECK(((host_tx->head - host_tx->tail) & NET_QUEUE_MASK) == NET_QUEUE_LEN);
    network_rx_queue(card);
    CHECK(received == 32);
    CHECK(network_queue_empty(tx));
    CHECK(((host_tx->head - host_tx->tail) & NET_QUEUE_MASK) == 32);

    /* A queued host backlog still gets notified after guest TX becomes empty. */
    unsigned notifications_before = notified;
    network_rx_queue(card);
    CHECK(notified == notifications_before + 1);
    for (unsigned i = 0; i < 32; i++)
        expect_packet(host_tx, &out, i);

    /* RX-only progress must wake a backend whose deferred queue was blocked. */
    enqueue(rx, 1);
    card->rx_deferred_pending = 1;
    notifications_before      = notified;
    network_rx_queue(card);
    CHECK(notified == notifications_before + 1);
    CHECK(scheduled_period == 200);
    card->rx_deferred_pending = 0;
    enqueue(rx, 2);
    notifications_before = notified;
    network_rx_queue(card);
    CHECK(notified == notifications_before);

    for (unsigned i = 0; i < SLIRP_PKT_BATCH; i++)
        free(slirp.pkt_tx_v[i].data);
    free(slirp.pkt.data);
    free(out.data);
    for (unsigned i = 0; i < NET_QUEUE_COUNT; i++)
        network_queue_clear(&card->queues[i]);
    free(card->queued_pkt.data);
    free(card);
    puts("Network queue tests passed");
    return 0;
}
