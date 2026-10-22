/* Real MPU-401 protocol and native PC-98 I/O; host MIDI/PIC/timers adapted. */
#include "../../src/sound/snd_mpu401.c"

uint64_t        TIMER_USEC = 1ULL << 32, tsc, timer_target;
static uint16_t active_irqs, base = 0xe0d0;
static int      irq = 6, input_attached;
static unsigned checks, failures, output_count, active_timers;
static uint8_t  output[1024];
static struct {
    uint8_t (*read)(uint16_t, void *);
    void (*write)(uint16_t, uint8_t, void *);
    void *priv;
} ports[65536];
#define CHECK(expr)                                                    \
    do {                                                               \
        checks++;                                                      \
        if (!(expr)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
            failures++;                                                \
        }                                                              \
    } while (0)

int
device_get_config_int(const char *key)
{
    return !strcmp(key, "irq") ? irq : 1;
}
int
device_get_config_hex16(const char *key)
{
    (void) key;
    return base;
}
void
io_sethandler(uint16_t addr, uint16_t size,
              uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *),
              uint32_t (*rl)(uint16_t, void *), void (*wb)(uint16_t, uint8_t, void *),
              void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *p)
{
    (void) rw;
    (void) rl;
    (void) ww;
    (void) wl;
    for (unsigned i = addr; i < (unsigned) addr + size; i++) {
        ports[i].read  = rb;
        ports[i].write = wb;
        ports[i].priv  = p;
    }
}
void
io_removehandler(uint16_t addr, uint16_t size,
                 uint8_t (*rb)(uint16_t, void *), uint16_t (*rw)(uint16_t, void *),
                 uint32_t (*rl)(uint16_t, void *), void (*wb)(uint16_t, uint8_t, void *),
                 void (*ww)(uint16_t, uint16_t, void *), void (*wl)(uint16_t, uint32_t, void *), void *p)
{
    (void) rb;
    (void) rw;
    (void) rl;
    (void) wb;
    (void) ww;
    (void) wl;
    for (unsigned i = addr; i < (unsigned) addr + size; i++)
        if (ports[i].priv == p)
            memset(&ports[i], 0, sizeof(ports[i]));
}
void
timer_add(pc_timer_t *t, void (*cb)(void *), void *p, int start)
{
    t->callback = cb;
    t->priv     = p;
    t->flags    = start ? TIMER_ENABLED : 0;
}
void
timer_enable(pc_timer_t *t)
{
    if (!(t->flags & TIMER_ENABLED))
        active_timers++;
    t->flags |= TIMER_ENABLED;
}
void
timer_disable(pc_timer_t *t)
{
    if (t->flags & TIMER_ENABLED)
        active_timers--;
    t->flags &= ~TIMER_ENABLED;
}
void
picint_common(uint16_t mask, int level, int set, uint8_t *p)
{
    (void) level;
    (void) p;
    if (set)
        active_irqs |= mask;
    else
        active_irqs &= ~mask;
}
void
midi_reset(void)
{
}
void
midi_clear_buffer(void)
{
}
void
midi_raw_out_byte(uint8_t value)
{
    if (output_count < sizeof(output))
        output[output_count++] = value;
}
void
midi_raw_out_rt_byte(uint8_t value)
{
    midi_raw_out_byte(value);
}
void
midi_raw_out_thru_rt_byte(uint8_t value)
{
    midi_raw_out_byte(value);
}
void
midi_in_handler(int set, void (*msg)(void *, uint8_t *, uint32_t),
                int (*sysex)(void *, uint8_t *, uint32_t, int), int (*remain)(void *), void *p)
{
    (void) msg;
    (void) sysex;
    (void) remain;
    (void) p;
    input_attached = set;
}
void
fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    abort();
}

static uint8_t
in(uint16_t port)
{
    return ports[port].read ? ports[port].read(port, ports[port].priv) : 0xff;
}
static void
out(uint16_t port, uint8_t value)
{
    if (ports[port].write)
        ports[port].write(port, value, ports[port].priv);
}

int
main(void)
{
    for (unsigned choice = 0; choice < 16; choice++) {
        base       = 0xc0d0 + choice * 0x400;
        irq        = (int[]) { 3, 5, 6, 12 }[choice & 3];
        mpu_t *mpu = mpu401_pc98_device.init(&mpu401_pc98_device);
        CHECK(mpu && mpu->addr == base && mpu->irq == irq && mpu->intelligent);
        CHECK(input_attached);
        CHECK(ports[base].read && ports[base + 2].read);
        CHECK(!ports[base + 1].read && !ports[base + 3].read);
        CHECK(!ports[0x330].read && !ports[0x2a20].write);
        CHECK(in(base + 2) & STATUS_INPUT_NOT_READY);
        out(base + 1, 0x3f); /* Odd byte lane must not decode. */
        CHECK(mpu->mode == M_INTELLIGENT);
        out(base + 2, 0x3f); /* Enter UART; acknowledgement is at DATA. */
        CHECK(mpu->mode == M_UART);
        CHECK(!(in(base + 2) & STATUS_INPUT_NOT_READY));
        CHECK(in(base) == MSG_MPU_ACK);
        output_count = 0;
        out(base, 0x90);
        out(base, 60);
        out(base, 100);
        CHECK(output_count == 3 && output[0] == 0x90 && output[1] == 60 && output[2] == 100);
        uint8_t message[] = { 0x80, 60, 0 };
        MPU401_InputMsg(mpu, message, sizeof(message));
        CHECK(active_irqs & (1u << irq));
        CHECK(in(base) == 0x80 && in(base) == 60 && in(base) == 0);
        CHECK(!(active_irqs & (1u << irq)));
        /* Hardware reset cancels an in-flight command and every timer. */
        out(base + 2, 0xff);
        CHECK(timer_is_enabled(&mpu->mpu401_reset_callback));
        out(base + 2, 0x3f);
        CHECK(mpu->state.cmd_pending == 0x40);
        /* Model callbacks already queued when the board reset arrives. */
        timer_enable(&mpu->mpu401_eoi_callback);
        timer_enable(&mpu->mpu401_event_callback);
        CHECK(active_timers == 3);
        mpu401_pc98_device.reset(mpu);
        CHECK(!timer_is_enabled(&mpu->mpu401_reset_callback));
        CHECK(!timer_is_enabled(&mpu->mpu401_eoi_callback));
        CHECK(!timer_is_enabled(&mpu->mpu401_event_callback) && !active_timers);
        CHECK(!mpu->queue_used && !mpu->state.reset && !mpu->state.cmd_pending);
        CHECK(mpu->mode == M_INTELLIGENT && !(active_irqs & (1u << irq)));
        /* Close while RESET is busy, a command is pending and IRQ is asserted. */
        out(base + 2, 0xff);
        out(base + 2, 0x3f);
        timer_enable(&mpu->mpu401_eoi_callback);
        timer_enable(&mpu->mpu401_event_callback);
        CHECK(active_timers == 3 && (active_irqs & (1u << irq)));
        mpu401_pc98_device.close(mpu);
        CHECK(!input_attached && !ports[base].read && !ports[base + 2].read);
        CHECK(!active_timers && !(active_irqs & (1u << irq)));
    }
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
