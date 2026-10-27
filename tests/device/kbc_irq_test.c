#include "../../src/device/kbc_at.c"

uint64_t tsc;
cpu_state_t cpu_state;
int isa_cycles;

static unsigned keyboard_irqs, auxiliary_irqs;
static pc_timer_t background_timer;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);   \
            exit(1);                                                         \
        }                                                                    \
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

int
machine_has_flags_ex(uintptr_t flags)
{
    return !!(flags & MACHINE_PS2_KBC);
}

void
picint_common(uint16_t num, int level, int set, uint8_t *irq_state)
{
    if (set) {
        keyboard_irqs += !!(num & (1 << 1));
        auxiliary_irqs += !!(num & (1 << 12));
    }
    if (irq_state)
        *irq_state = set;
}

void
fdc37mx0x_watchdog_reset_ext(int reason)
{
}

int
keyboard_recv(uint16_t key)
{
    return 0;
}

void
t3100e_notify_set(uint8_t value)
{
}

static void
background_tick(void *priv)
{
    timer_advance_u64(&background_timer, 1000000ULL * TIMER_USEC);
}

void
rivatimer_init(void)
{
    /* timer_process expects another timer after removing the expired head. */
    timer_add(&background_timer, background_tick, NULL, 0);
    timer_set_delay_u64(&background_timer, 1000000ULL * TIMER_USEC);
}

static void
setup(atkbc_t *dev, unsigned cpu_mhz, int asic)
{
    timer_close();
    memset(dev, 0, sizeof(*dev));
    TIMER_USEC = (uint64_t) cpu_mhz << 32;
    timer_init();
    keyboard_irqs = auxiliary_irqs = 0;
    dev->misc_flags = FLAG_PS2;
    dev->is_asic = asic;
    dev->irq[0] = 1;
    dev->irq[1] = 12;
    dev->mem[0x20] = CCB_ENABLEKINT | CCB_ENABLEMINT;
    timer_add(&dev->kbc_irq_timer, kbc_at_irq, dev, 0);
}

static void
advance(unsigned cpu_mhz, unsigned microseconds)
{
    tsc += (uint64_t) cpu_mhz * microseconds;
    timer_process();
}

static void
output(atkbc_t *dev, unsigned channel, uint8_t byte)
{
    kbc_send_to_ob(dev, byte, channel, 0);
}

int
main(void)
{
    atkbc_t dev;

    for (unsigned cpu_mhz = 8; cpu_mhz <= 512; cpu_mhz *= 2) {
        for (unsigned channel = 1; channel <= 2; channel++) {
            setup(&dev, cpu_mhz, 0);
            output(&dev, channel, 0x41);
            advance(cpu_mhz, 2);
            CHECK(keyboard_irqs == 0 && auxiliary_irqs == 0);
            advance(cpu_mhz, 2);
            CHECK(keyboard_irqs == (channel == 1));
            CHECK(auxiliary_irqs == (channel == 2));
            CHECK(dev.do_irq == 0);
            CHECK(kbc_at_port_1_read(0x60, &dev) == 0x41);
            advance(cpu_mhz, 4);
            CHECK(keyboard_irqs + auxiliary_irqs == 1);

            setup(&dev, cpu_mhz, 0);
            output(&dev, channel, 0x41);
            advance(cpu_mhz, 1);
            CHECK(kbc_at_port_1_read(0x60, &dev) == 0x41);
            CHECK(!(dev.status & STAT_OFULL));
            CHECK(dev.do_irq == 0);
            CHECK(!timer_is_enabled(&dev.kbc_irq_timer));
            advance(cpu_mhz, 4);
            CHECK(keyboard_irqs == 0 && auxiliary_irqs == 0);

            /* A subsequent byte still gets its own delayed interrupt. */
            output(&dev, channel, 0x55);
            advance(cpu_mhz, 2);
            CHECK(keyboard_irqs == 0 && auxiliary_irqs == 0);
            advance(cpu_mhz, 2);
            CHECK(keyboard_irqs == (channel == 1));
            CHECK(auxiliary_irqs == (channel == 2));
            CHECK(kbc_at_port_1_read(0x60, &dev) == 0x55);

            setup(&dev, cpu_mhz, 1);
            output(&dev, channel, 0x41);
            CHECK(keyboard_irqs == (channel == 1));
            CHECK(auxiliary_irqs == (channel == 2));
            CHECK(kbc_at_port_1_read(0x60, &dev) == 0x41);
            advance(cpu_mhz, 4);
            CHECK(keyboard_irqs + auxiliary_irqs == 1);
        }

        setup(&dev, cpu_mhz, 0);
        dev.mem[0x20] |= CCB_TRANSLATE;
        output(&dev, 1, 0x83);
        CHECK(kbc_at_port_1_read(0x60, &dev) == 0x41);
        advance(cpu_mhz, 4);
        CHECK(keyboard_irqs == 0);

        setup(&dev, cpu_mhz, 0);
        dev.misc_flags = 0;
        output(&dev, 1, 0x41);
        CHECK(keyboard_irqs == 1);
        CHECK(dev.irq_state == 1);
        CHECK(kbc_at_port_1_read(0x60, &dev) == 0x41);
        CHECK(dev.irq_state == 0);
        advance(cpu_mhz, 4);
        CHECK(keyboard_irqs == 1);

        atkbc_t *closing = calloc(1, sizeof(*closing));
        CHECK(closing != NULL);
        setup(closing, cpu_mhz, 0);
        output(closing, 1, 0x41);
        kbc_at_close(closing);
        CHECK(background_timer.prev == NULL && background_timer.next == NULL);
        advance(cpu_mhz, 4);
        CHECK(keyboard_irqs == 0 && auxiliary_irqs == 0);
    }
    timer_close();
    puts("KBC delayed IRQ tests passed");
    return 0;
}
