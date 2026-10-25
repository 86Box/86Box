#include "../../src/network/net_rtl8139.c"

uint64_t       tsc;
int            cpu_pci_speed;
netcard_conf_t net_cards_conf[NET_CARD_MAX];

static unsigned   callbacks;
static pc_timer_t background_timer;
static netcard_t  test_nic;

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            exit(1);                                                        \
        }                                                                   \
    } while (0)

void
fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}

void
background_tick(void *priv)
{
    timer_advance_u64(&background_timer, 1000000ULL * TIMER_USEC);
}

void
rivatimer_init(void)
{
    /* Production always has other active timers; timer_remove_head requires one. */
    timer_add(&background_timer, background_tick, NULL, 0);
    timer_set_delay_u64(&background_timer, 1000000ULL * TIMER_USEC);
}

void
pci_irq(uint8_t slot, uint8_t pci_int, int level, int set, uint8_t *irq_state)
{
    *irq_state = set;
}

static void
counted_timer(void *priv)
{
    callbacks++;
    rtl8139_timer(priv);
}

static void
setup(RTL8139State *s, unsigned cpu_mhz, unsigned pci_hz)
{
    timer_close();
    memset(s, 0, sizeof(*s));
    tsc           = 0;
    TIMER_USEC    = (uint64_t) cpu_mhz << 32;
    cpu_pci_speed = pci_hz;
    callbacks     = 0;
    timer_init();
    timer_add(&s->timer, counted_timer, s, 0);
    s->clock_enabled = 1;
    s->IntrMask      = PCSTimeout;
    s->nic           = &test_nic;
    rtl8139_speed_changed(s);
}

static void
advance(uint64_t elapsed_cycles)
{
    uint64_t target = tsc + elapsed_cycles;
    while (timer_target <= target) {
        CHECK(callbacks < 10000);
        tsc = timer_target;
        timer_process();
    }
    tsc = target;
}

static uint32_t
counter(RTL8139State *s)
{
    rtl8139_sync_tctr(s);
    return s->TCTR;
}

int
main(void)
{
    RTL8139State s;

    /* Polling works without a timer interrupt, with only one callback per second. */
    setup(&s, 100, 25000000);
    advance(1000000);
    CHECK(counter(&s) == 250000);
    CHECK(callbacks == 0);
    advance(99000000);
    CHECK(counter(&s) == 25000000);
    CHECK(callbacks == 1);
    CHECK(s.IntrStatus == 0);

    setup(&s, 100, 25000000);
    rtl8139_timerint_write(&s, 100);
    advance(399);
    CHECK(counter(&s) == 99);
    CHECK(s.irq_state == 0);
    advance(1);
    CHECK(counter(&s) == 100);
    CHECK(s.irq_state == 1);
    CHECK(callbacks == 1);
    s.IntrStatus = 0;
    rtl8139_update_irq(&s);
    advance(400);
    CHECK(s.IntrStatus == 0);

    /* A match behind (or equal to) the counter must wait for a full wrap. */
    setup(&s, 100, 25000000);
    advance(800);
    rtl8139_timerint_write(&s, 100);
    advance(((1ULL << 32) - 100) * 4 - 1);
    CHECK(!(s.IntrStatus & PCSTimeout));
    advance(1);
    CHECK(counter(&s) == 100);
    CHECK(s.IntrStatus & PCSTimeout);
    s.IntrStatus = 0;
    rtl8139_timerint_write(&s, 100);
    advance(4);
    CHECK(!(s.IntrStatus & PCSTimeout));
    advance(((1ULL << 32) - 1) * 4);
    CHECK(counter(&s) == 100);
    CHECK(s.IntrStatus & PCSTimeout);

    rtl8139_reset(&s);
    CHECK(counter(&s) == 0);
    CHECK(s.TimerInt == 0);
    CHECK(s.IntrStatus == 0);
    CHECK(s.irq_state == 0);
    CHECK(s.clock_enabled);
    unsigned before_reset = callbacks;
    advance(1000000);
    CHECK(counter(&s) == 250000);
    CHECK(callbacks == before_reset);

    /* Disable cancels a pending match, but not the general-purpose counter. */
    setup(&s, 100, 25000000);
    rtl8139_timerint_write(&s, 100);
    advance(200);
    rtl8139_timerint_write(&s, 0);
    advance(400);
    CHECK(counter(&s) == 150);
    CHECK(s.IntrStatus == 0);
    CHECK(callbacks == 0);

    setup(&s, 100, 33333333);
    advance(100000000);
    CHECK(counter(&s) == ((uint128_t) tsc << 32) / s.TCTR_period);
    CHECK(callbacks == 1);
    CHECK(s.IntrStatus == 0);

    setup(&s, 100, 25000000);
    rtl8139_timerint_write(&s, 100);
    advance(200);
    rtl8139_timerint_write(&s, 200);
    advance(200);
    CHECK(s.IntrStatus == 0);
    advance(400);
    CHECK(counter(&s) == 200);
    CHECK(s.IntrStatus & PCSTimeout);

    /* Resetting the counter keeps the PCI clock's fractional phase. */
    setup(&s, 100, 25000000);
    advance(202);
    rtl8139_tctr_reset(&s);
    CHECK(counter(&s) == 0);
    rtl8139_timerint_write(&s, 1);
    advance(1);
    CHECK(counter(&s) == 0);
    advance(1);
    CHECK(counter(&s) == 1);
    CHECK(s.IntrStatus & PCSTimeout);

    setup(&s, 100, 25000000);
    rtl8139_timerint_write(&s, 100);
    advance(200);
    rtl8139_set_clock(&s, 'H');
    advance(100000000);
    CHECK(counter(&s) == 50);
    CHECK(s.IntrStatus == 0);
    rtl8139_set_clock(&s, 'R');
    advance(200);
    CHECK(counter(&s) == 100);
    CHECK(s.IntrStatus & PCSTimeout);

    setup(&s, 100, 25000000);
    advance(202);
    rtl8139_set_clock(&s, 'H');
    advance(5);
    CHECK(counter(&s) == 50);
    rtl8139_set_clock(&s, 'R');
    advance(1);
    CHECK(counter(&s) == 51);

    /* TSC writes rebase registered deadlines without changing NIC time. */
    setup(&s, 100, 25000000);
    advance(202);
    CHECK(counter(&s) == 50);
    timer_set_new_tsc(1ULL << 50);
    CHECK(counter(&s) == 50);
    advance(2);
    CHECK(counter(&s) == 51);
    timer_set_new_tsc(0);
    CHECK(counter(&s) == 51);

    setup(&s, 100, 25000000);
    rtl8139_timerint_write(&s, 100);
    advance(202);
    timer_set_new_tsc(1ULL << 50);
    advance(197);
    CHECK(s.IntrStatus == 0);
    advance(1);
    CHECK(counter(&s) == 100);
    CHECK(s.IntrStatus & PCSTimeout);

    /* A speed change must account for old elapsed cycles at the old rate. */
    setup(&s, 100, 25000000);
    advance(202);
    TIMER_USEC    = 200ULL << 32;
    cpu_pci_speed = 50000000;
    rtl8139_speed_changed(&s);
    CHECK(counter(&s) == 50);
    advance(2);
    CHECK(counter(&s) == 51);
    TIMER_USEC = 100ULL << 32;
    rtl8139_speed_changed(&s);
    advance(20);
    CHECK(counter(&s) == 61);

    setup(&s, 100, 25000000);
    rtl8139_timerint_write(&s, 100);
    advance(202);
    TIMER_USEC = 200ULL << 32;
    rtl8139_speed_changed(&s);
    advance(395);
    CHECK(counter(&s) == 99);
    CHECK(s.IntrStatus == 0);
    advance(1);
    CHECK(counter(&s) == 100);
    CHECK(s.IntrStatus & PCSTimeout);

    /* Fractional deadlines must not loop forever at the same integer TSC. */
    setup(&s, 100, 33333333);
    rtl8139_timerint_write(&s, 1);
    advance(3);
    CHECK(s.IntrStatus == 0);
    advance(1);
    CHECK(s.IntrStatus & PCSTimeout);
    CHECK(callbacks == 1);
    CHECK(counter(&s) == 1);

    /* Compare arbitrary polling against the original fixed-point PCI period. */
    setup(&s, 100, 33333333);
    uint64_t period = s.TCTR_period;
    for (unsigned i = 1; i <= 1000; i++) {
        advance(137);
        CHECK(counter(&s) == ((uint128_t) tsc << 32) / period);
    }
    CHECK(callbacks == 0);

    /* A late callback still detects a match and advances all elapsed ticks. */
    setup(&s, 100, 25000000);
    rtl8139_timerint_write(&s, 100);
    tsc = 1000;
    timer_process();
    CHECK(counter(&s) == 250);
    CHECK(s.IntrStatus & PCSTimeout);
    CHECK(callbacks == 1);

    setup(&s, 100, 25000000);
    s.IntrMask = 0;
    rtl8139_timerint_write(&s, 1);
    advance(4);
    CHECK(s.IntrStatus & PCSTimeout);
    CHECK(s.irq_state == 0);
    s.IntrMask = PCSTimeout;
    rtl8139_update_irq(&s);
    CHECK(s.irq_state == 1);

    timer_close();
    puts("RTL8139 timer tests passed");
    return 0;
}
