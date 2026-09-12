/*
 * 86Box: IBM PC JX motherboard.
 *
 * The decoder owns CPU visibility; video and removable cartridges retain
 * their own stable backing stores. Pull-high open bus and wired-AND reads
 * during contention are explicit approximations, not measured JX circuitry.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/plat.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/cartridge.h>
#include <86box/cassette.h>
#include <86box/keyboard.h>
#include <86box/nmi.h>
#include <86box/nvr.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/snd_sn76489.h>
#include <86box/video.h>
#include <86box/vid_cga_comp.h>
#include <86box/vid_pcjx.h>
#include <86box/m_pcjx.h>

/* Private PC JX cable keyboard. Include after timer.h and keyboard.h.
 * The callback receives PC6, not cable voltage or CPU NMI. The motherboard
 * latches rising edges and independently gates the latch with A0 bit 7.
 * The growable queue schedules host events; it is not receiver hardware.
 * Repeat arbitration for simultaneous holds is undocumented: this model
 * repeats each held raw key and orders coincident deadlines by raw code.
 */
typedef struct pcjx_keyboard_t {
    pc_timer_t wire_timer;
    pc_timer_t repeat_timer;
    void (*set_level)(void *priv, int level);
    void *level_priv;
    uint8_t *queue;
    size_t queue_capacity;
    size_t queue_head;
    size_t queue_count;
    uint8_t host_down[512];
    uint16_t raw_holds[128];
    uint64_t repeat_left[128];
    uint64_t repeat_delay;
    uint16_t frame;
    uint8_t half;
    uint8_t running;
    uint8_t level;
} pcjx_keyboard_t;

extern const scancode pcjx_scancodes[512];

static void
pcjx_keyboard_level(pcjx_keyboard_t *kbd, int level)
{
    if (kbd->level != level) {
        kbd->level = level;
        kbd->set_level(kbd->level_priv, level);
    }
}

static void
pcjx_keyboard_enqueue(pcjx_keyboard_t *kbd, uint8_t value)
{
    if (kbd->queue_count == kbd->queue_capacity) {
        size_t capacity = kbd->queue_capacity ? kbd->queue_capacity * 2 : 32;
        uint8_t *queue;

        if (capacity <= kbd->queue_capacity)
            fatal("PC JX: keyboard event queue size overflow\n");
        queue = (uint8_t *) malloc(capacity);
        if (!queue)
            fatal("PC JX: cannot grow keyboard event queue\n");
        for (size_t i = 0; i < kbd->queue_count; i++)
            queue[i] = kbd->queue[(kbd->queue_head + i) % kbd->queue_capacity];
        free(kbd->queue);
        kbd->queue = queue;
        kbd->queue_capacity = capacity;
        kbd->queue_head = 0;
    }
    kbd->queue[(kbd->queue_head + kbd->queue_count) % kbd->queue_capacity] = value;
    kbd->queue_count++;
    if (!kbd->running) {
        kbd->running = 1;
        kbd->half = 0;
        timer_set_delay_u64(&kbd->wire_timer, 0);
    }
}

static void
pcjx_keyboard_wire_tick(void *priv)
{
    pcjx_keyboard_t *kbd = (pcjx_keyboard_t *) priv;

    if (kbd->half == 20) {
        pcjx_keyboard_level(kbd, 0);
        kbd->half = 0;
        timer_advance_u64(&kbd->wire_timer, TIMER_USEC * 4840);
        return;
    }
    if (!kbd->half) {
        uint8_t value;
        unsigned parity = 1;

        if (!kbd->queue_count) {
            kbd->running = 0;
            return;
        }
        value = kbd->queue[kbd->queue_head];
        kbd->queue_head = (kbd->queue_head + 1) % kbd->queue_capacity;
        kbd->queue_count--;
        for (unsigned i = 0; i < 8; i++)
            parity ^= (value >> i) & 1;
        kbd->frame = 1 | ((uint16_t) value << 1) | (parity << 9);
    }
    pcjx_keyboard_level(kbd, ((kbd->frame >> (kbd->half >> 1)) & 1) ^ (kbd->half & 1));
    kbd->half++;
    timer_advance_u64(&kbd->wire_timer, TIMER_USEC * 220);
}

/* Account emulated time only. Reads and duplicate host downs never reach this
 * state machine. All documented raw keys are eligible for hardware repeat;
 * BIOS repeat filtering remains entirely in the guest.
 */
static void
pcjx_keyboard_repeat_sync(pcjx_keyboard_t *kbd)
{
    uint64_t remaining;
    uint64_t elapsed;

    if (!kbd->repeat_delay)
        return;
    remaining = (uint64_t) timer_get_remaining_u64(&kbd->repeat_timer);
    elapsed = kbd->repeat_delay - remaining;
    timer_disable(&kbd->repeat_timer);
    kbd->repeat_delay = 0;
    for (unsigned raw = 1; raw < 128; raw++) {
        if (!kbd->raw_holds[raw])
            continue;
        if (kbd->repeat_left[raw] <= elapsed) {
            pcjx_keyboard_enqueue(kbd, raw);
            kbd->repeat_left[raw] = (TIMER_USEC * 1000000ULL) / 11;
        } else
            kbd->repeat_left[raw] -= elapsed;
    }
}

static void
pcjx_keyboard_repeat_arm(pcjx_keyboard_t *kbd, int from_callback)
{
    uint64_t delay = UINT64_MAX;

    for (unsigned raw = 1; raw < 128; raw++) {
        if (kbd->raw_holds[raw] && kbd->repeat_left[raw] < delay)
            delay = kbd->repeat_left[raw];
    }
    if (delay != UINT64_MAX) {
        kbd->repeat_delay = delay;
        if (from_callback)
            timer_advance_u64(&kbd->repeat_timer, delay);
        else
            timer_set_delay_u64(&kbd->repeat_timer, delay);
    }
}

static void
pcjx_keyboard_repeat_tick(void *priv)
{
    pcjx_keyboard_t *kbd = (pcjx_keyboard_t *) priv;

    pcjx_keyboard_repeat_sync(kbd);
    pcjx_keyboard_repeat_arm(kbd, 1);
}

static void
pcjx_keyboard_input(uint16_t scan, int down, void *priv)
{
    pcjx_keyboard_t *kbd = (pcjx_keyboard_t *) priv;
    const scancode *codes;

    if (scan >= 512)
        return;
    down = !!down;
    codes = &pcjx_scancodes[scan];
    if (!codes->mk[0] || kbd->host_down[scan] == down)
        return;
    pcjx_keyboard_repeat_sync(kbd);
    kbd->host_down[scan] = down;
    if (down) {
        for (unsigned i = 0; codes->mk[i]; i++) {
            unsigned raw = codes->mk[i];
            if (!kbd->raw_holds[raw]++) {
                pcjx_keyboard_enqueue(kbd, raw);
                kbd->repeat_left[raw] = TIMER_USEC * 660000;
            }
        }
    } else {
        /* Release gestures in reverse order; a modifier also held physically
         * stays down until its last owner releases it.
         */
        for (unsigned i = 0; codes->brk[i]; i++) {
            unsigned raw = codes->brk[i] & 0x7f;
            if (!--kbd->raw_holds[raw]) {
                pcjx_keyboard_enqueue(kbd, codes->brk[i]);
                kbd->repeat_left[raw] = 0;
            }
        }
    }
    pcjx_keyboard_repeat_arm(kbd, 0);
}

static void
pcjx_keyboard_host_input(uint16_t scan, int down, void *priv)
{
    /* Frontends deliver input on their UI thread. The recursive execution
     * lock also covers timer-list mutation and sender queue growth. */
    startblit();
    pcjx_keyboard_input(scan, down, priv);
    endblit();
}

/* A CPU-only reset discards stale sender/host state, not board latches or RAM.
 * Ordinary all-up is instead delivered through the normalized input hook so
 * that queued releases retain their frame and gap ordering.
 */
static void
pcjx_keyboard_reset(pcjx_keyboard_t *kbd)
{
    timer_disable(&kbd->wire_timer);
    timer_disable(&kbd->repeat_timer);
    memset(kbd->host_down, 0, sizeof(kbd->host_down));
    memset(kbd->raw_holds, 0, sizeof(kbd->raw_holds));
    memset(kbd->repeat_left, 0, sizeof(kbd->repeat_left));
    kbd->repeat_delay = 0;
    kbd->queue_head = kbd->queue_count = 0;
    kbd->frame = 0;
    kbd->half = kbd->running = 0;
    pcjx_keyboard_level(kbd, 0);
}

static void
pcjx_keyboard_init(pcjx_keyboard_t *kbd, void (*set_level)(void *, int), void *priv)
{
    memset(kbd, 0, sizeof(*kbd));
    kbd->set_level = set_level;
    kbd->level_priv = priv;
    timer_add(&kbd->wire_timer, pcjx_keyboard_wire_tick, kbd, 0);
    timer_add(&kbd->repeat_timer, pcjx_keyboard_repeat_tick, kbd, 0);
    set_level(priv, 0);
    keyboard_scan = 1;
    keyboard_set_input_handler(pcjx_keyboard_host_input, kbd);
}

static void
pcjx_keyboard_close(pcjx_keyboard_t *kbd)
{
    keyboard_set_input_handler(NULL, NULL);
    pcjx_keyboard_reset(kbd);
    free(kbd->queue);
    kbd->queue = NULL;
    kbd->queue_capacity = 0;
}

/* Private PC JX clock option. Integrate in m_pcjx.c before pcjx_t.
 * Requires stdint.h, stdlib.h, string.h, time.h, 86box.h, timer.h, nvr.h.
 * Board owns I/O registration; priv for these handlers is pcjx_clock_t *.
 * Compatible MSM6242/RTC-72421 control semantics are a modeling assumption;
 * no RTC interrupt output is wired to the motherboard.
 */
typedef struct pcjx_clock_t {
    nvr_t nvr;
    pc_timer_t periodic_timer;
    pc_timer_t pulse_timer;
    uint64_t stopped_us;
    uint8_t hour24;
    uint8_t hold_dirty;
    uint8_t pending_second;
} pcjx_clock_t;

static const uint8_t pcjx_clock_masks[13] = {
    0xf, 7, 0xf, 7, 0xf, 7, 0xf, 3, 0xf, 1, 0xf, 0xf, 7
};

static int
pcjx_clock_pair(const uint8_t *regs, unsigned int index)
{
    return regs[index] + 10 * regs[index + 1];
}

/* Reject an incomplete digit transfer, never normalize its intermediate date.
 * The two-digit calendar uses 1980..2079, including leap year 2000.
 */
static int
pcjx_clock_calendar(pcjx_clock_t *clock, struct tm *tm)
{
    const uint8_t *r = clock->nvr.regs;
    int hour = r[4] + 10 * (r[5] & 3);

    for (unsigned int i = 0; i < 13; i++) {
        if (r[i] & ~pcjx_clock_masks[i])
            return 0;
        if (i != 5 && r[i] > 9)
            return 0;
    }
    if (clock->hour24) {
        if ((r[5] & 4) || hour > 23)
            return 0;
    } else {
        if (hour < 1 || hour > 12)
            return 0;
        hour = (hour % 12) + ((r[5] & 4) ? 12 : 0);
    }
    memset(tm, 0, sizeof(*tm));
    tm->tm_sec = pcjx_clock_pair(r, 0);
    tm->tm_min = pcjx_clock_pair(r, 2);
    tm->tm_hour = hour;
    tm->tm_mday = pcjx_clock_pair(r, 6);
    tm->tm_mon = pcjx_clock_pair(r, 8) - 1;
    tm->tm_year = pcjx_clock_pair(r, 10);
    if (tm->tm_year < 80)
        tm->tm_year += 100;
    tm->tm_wday = r[12];
    return tm->tm_sec < 60 && tm->tm_min < 60 && r[4] <= 9 &&
           tm->tm_mon >= 0 && tm->tm_mon < 12 && tm->tm_wday < 7 &&
           tm->tm_mday >= 1 &&
           tm->tm_mday <= nvr_get_days(tm->tm_mon + 1, tm->tm_year + 1900);
}

static void
pcjx_clock_set_calendar(pcjx_clock_t *clock, const struct tm *tm)
{
    uint8_t *r = clock->nvr.regs;
    int hour = tm->tm_hour;
    if (!clock->hour24) {
        hour %= 12;
        if (!hour)
            hour = 12;
    }
    r[0] = tm->tm_sec % 10;
    r[1] = tm->tm_sec / 10;
    r[2] = tm->tm_min % 10;
    r[3] = tm->tm_min / 10;
    r[4] = hour % 10;
    r[5] = (hour / 10) | ((!clock->hour24 && tm->tm_hour >= 12) ? 4 : 0);
    r[6] = tm->tm_mday % 10;
    r[7] = tm->tm_mday / 10;
    r[8] = (tm->tm_mon + 1) % 10;
    r[9] = (tm->tm_mon + 1) / 10;
    r[10] = tm->tm_year % 10;
    r[11] = (tm->tm_year % 100) / 10;
    r[12] = tm->tm_wday;
    nvr_dosave = 1;
}

static void
pcjx_clock_sync(pcjx_clock_t *clock)
{
    struct tm tm;
    if (pcjx_clock_calendar(clock, &tm))
        nvr_time_set(&tm);
}

static void
pcjx_clock_pulse_end(void *priv)
{
    pcjx_clock_t *clock = (pcjx_clock_t *) priv;
    clock->nvr.regs[13] &= ~4;
    nvr_dosave = 1;
}

/* MASK inhibits the option's local IRQ flag; ITRPT holds the flag until a
 * software clear, STND makes a 1/128-second pulse. No picint() is involved.
 */
static void
pcjx_clock_event(pcjx_clock_t *clock, unsigned int rate)
{
    uint8_t *r = clock->nvr.regs;
    if ((r[14] & 1) || ((r[14] >> 2) != rate))
        return;
    r[13] |= 4;
    nvr_dosave = 1;
    if (!(r[14] & 2))
        timer_set_delay_u64(&clock->pulse_timer, (TIMER_USEC * 15625ULL) / 2);
}

static void
pcjx_clock_periodic(void *priv)
{
    pcjx_clock_t *clock = (pcjx_clock_t *) priv;
    pcjx_clock_event(clock, 0);
    timer_advance_u64(&clock->periodic_timer, TIMER_USEC * 15625ULL);
}

static void
pcjx_clock_periodic_update(pcjx_clock_t *clock)
{
    const uint8_t *r = clock->nvr.regs;
    timer_disable(&clock->periodic_timer);
    if (!(r[15] & 3) && !(r[14] & 0xd))
        timer_set_delay_u64(&clock->periodic_timer, TIMER_USEC * 15625ULL);
}

/* Advance from the device's calendar, not nvr_time_get(): generic NVR ticks
 * its internal clock even while this chip is held/stopped, and its weekday
 * getter recomputes the weekday instead of preserving the guest's digit.
 */
static void
pcjx_clock_advance(pcjx_clock_t *clock)
{
    struct tm tm;
    if (!pcjx_clock_calendar(clock, &tm))
        return;
    if (++tm.tm_sec == 60) {
        tm.tm_sec = 0;
        pcjx_clock_event(clock, 2);
        if (++tm.tm_min == 60) {
            tm.tm_min = 0;
            pcjx_clock_event(clock, 3);
            if (++tm.tm_hour == 24) {
                tm.tm_hour = 0;
                tm.tm_wday = (tm.tm_wday + 1) % 7;
                if (++tm.tm_mday > nvr_get_days(tm.tm_mon + 1, tm.tm_year + 1900)) {
                    tm.tm_mday = 1;
                    if (++tm.tm_mon == 12) {
                        tm.tm_mon = 0;
                        if (++tm.tm_year == 180)
                            tm.tm_year = 80;
                    }
                }
            }
        }
    }
    pcjx_clock_set_calendar(clock, &tm);
    nvr_time_set(&tm);
}

static void
pcjx_clock_tick(nvr_t *nvr)
{
    pcjx_clock_t *clock = (pcjx_clock_t *) nvr->data;
    if (nvr->regs[15] & 3)
        return;
    pcjx_clock_event(clock, 1);
    if (nvr->regs[13] & 1)
        clock->pending_second = 1;
    else
        pcjx_clock_advance(clock);
}

static void
pcjx_clock_release(pcjx_clock_t *clock)
{
    clock->nvr.regs[13] &= ~1;
    if (clock->pending_second && !clock->hold_dirty)
        pcjx_clock_advance(clock);
    clock->pending_second = 0;
    clock->hold_dirty = 0;
    pcjx_clock_sync(clock);
    nvr_dosave = 1;
}

static void
pcjx_clock_divider_reset(pcjx_clock_t *clock)
{
    clock->nvr.onesec_cnt = 0;
    clock->stopped_us = 10000;
    clock->pending_second = 0;
    if (!(clock->nvr.regs[15] & 3))
        timer_set_delay_u64(&clock->nvr.onesec_time, TIMER_USEC * 10000ULL);
    pcjx_clock_periodic_update(clock);
}

static void
pcjx_clock_adjust(pcjx_clock_t *clock)
{
    struct tm tm;
    if (!pcjx_clock_calendar(clock, &tm))
        return;
    int carry = tm.tm_sec >= 30;
    tm.tm_sec = carry ? 59 : 0;
    pcjx_clock_set_calendar(clock, &tm);
    if (carry)
        pcjx_clock_advance(clock);
    pcjx_clock_divider_reset(clock);
    if (clock->nvr.regs[13] & 1)
        clock->hold_dirty = 1;
    pcjx_clock_sync(clock);
}

static uint8_t
pcjx_clock_read(uint16_t port, void *priv)
{
    pcjx_clock_t *clock = (pcjx_clock_t *) priv;
    unsigned int index = port & 15;
    if (index < 13) {
        uint8_t mask = pcjx_clock_masks[index];
        if (index == 5 && clock->hour24)
            mask &= ~4;
        return clock->nvr.regs[index] & mask;
    }
    /* BUSY is read-only and zero: acquisition and second carries are atomic.
     * 30-second ADJ is a command, not a latched busy interval.
     */
    return clock->nvr.regs[index] & ((index == 13) ? 5 : 15);
}

static void
pcjx_clock_write(uint16_t port, uint8_t value, void *priv)
{
    pcjx_clock_t *clock = (pcjx_clock_t *) priv;
    uint8_t *r = clock->nvr.regs;
    unsigned int index = port & 15;
    value &= 15;
    if (index < 13) {
        value &= pcjx_clock_masks[index];
        if (index == 5 && clock->hour24)
            value &= ~4;
        r[index] = value;
        if (r[13] & 1)
            clock->hold_dirty = 1;
        else
            pcjx_clock_sync(clock);
    } else if (index == 13) {
        uint8_t old = r[13];
        /* IRQ FLAG may only be cleared by software, never set by writing 1. */
        r[13] = (old & value & 4) | (value & 1);
        if (!(value & 4))
            timer_disable(&clock->pulse_timer);
        if (!(old & 1) && (value & 1)) {
            clock->hold_dirty = 0;
            clock->pending_second = 0;
        }
        if (value & 8)
            pcjx_clock_adjust(clock);
        if ((old & 1) && !(value & 1))
            pcjx_clock_release(clock);
    } else if (index == 14) {
        r[14] = value;
        if (value & 1) {
            r[13] &= ~4;
            timer_disable(&clock->pulse_timer);
        } else if (value & 2)
            timer_disable(&clock->pulse_timer);
        pcjx_clock_periodic_update(clock);
    } else {
        uint8_t old = r[15];
        struct tm tm;
        int valid = pcjx_clock_calendar(clock, &tm);
        if ((old & 1) && !(value & 1)) {
            clock->hour24 = !!(value & 4);
            if (valid)
                pcjx_clock_set_calendar(clock, &tm);
        }
        /* The hour selector is latched on RESET's falling edge. */
        if (!(value & 1))
            value = (value & ~4) | (clock->hour24 ? 4 : 0);
        r[15] = value;
        if (!(old & 3) && (value & 3)) {
            clock->stopped_us = timer_get_remaining_us(&clock->nvr.onesec_time);
            timer_disable(&clock->nvr.onesec_time);
        }
        if ((old ^ value) & 1)
            pcjx_clock_divider_reset(clock);
        if ((old & 3) && !(value & 3))
            timer_set_delay_u64(&clock->nvr.onesec_time, TIMER_USEC * clock->stopped_us);
        if (value & 3) {
            timer_disable(&clock->pulse_timer);
            r[13] &= ~4;
        }
        pcjx_clock_periodic_update(clock);
        pcjx_clock_sync(clock);
    }
    nvr_dosave = 1;
}

static void
pcjx_clock_nvr_reset(nvr_t *nvr)
{
    pcjx_clock_t *clock = (pcjx_clock_t *) nvr->data;
    struct tm tm = { 0 };
    memset(nvr->regs, 0, 16);
    nvr->regs[14] = 1;
    nvr->regs[15] = 4;
    clock->hour24 = 1;
    tm.tm_year = 80;
    tm.tm_mon = 0;
    tm.tm_mday = 1;
    tm.tm_wday = 2;
    pcjx_clock_set_calendar(clock, &tm);
}

static void
pcjx_clock_nvr_start(nvr_t *nvr)
{
    pcjx_clock_t *clock = (pcjx_clock_t *) nvr->data;
    struct tm tm;
    clock->hour24 = !!(nvr->regs[15] & 4);
    if ((nvr->regs[13] & ~15) || (nvr->regs[14] & ~15) ||
        (nvr->regs[15] & ~15) || !pcjx_clock_calendar(clock, &tm)) {
        pcjx_clock_nvr_reset(nvr);
        nvr->is_new = 1;
    }
    /* Battery persistence is calendar/control state, not an in-flight CPU
     * acquisition. Leave STOP and the programmed output mode intact.
     */
    nvr->regs[13] = 0;
    nvr->regs[15] &= ~1;
    clock->hold_dirty = 0;
    clock->pending_second = 0;
    clock->stopped_us = 10000;
    if (time_sync & TIME_SYNC_ENABLED) {
        nvr_time_get(&tm);
        /* Unlike the generic getter's approximation, host tm_wday is not
         * needed here: derive a correct Gregorian weekday once at startup.
         */
        int year = tm.tm_year + 1900;
        int month = tm.tm_mon + 1;
        static const int month_offset[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
        if (month < 3)
            year--;
        tm.tm_wday = (year + year / 4 - year / 100 + year / 400 +
                      month_offset[month - 1] + tm.tm_mday) % 7;
        pcjx_clock_set_calendar(clock, &tm);
    }
    pcjx_clock_sync(clock);
    nvr->onesec_cnt = 0;
    timer_disable(&nvr->onesec_time);
    if (!(nvr->regs[15] & 2))
        timer_set_delay_u64(&nvr->onesec_time, TIMER_USEC * 10000ULL);
    pcjx_clock_periodic_update(clock);
    nvr_dosave = 1;
}

static void
pcjx_clock_init(pcjx_clock_t *clock)
{
    memset(clock, 0, sizeof(*clock));
    clock->nvr.size = 16;
    clock->nvr.irq = -1;
    clock->nvr.data = clock;
    clock->nvr.reset = pcjx_clock_nvr_reset;
    clock->nvr.start = pcjx_clock_nvr_start;
    clock->nvr.tick = pcjx_clock_tick;
    timer_add(&clock->periodic_timer, pcjx_clock_periodic, clock, 0);
    timer_add(&clock->pulse_timer, pcjx_clock_pulse_end, clock, 0);
    nvr_init(&clock->nvr);
}

static void
pcjx_clock_warm_reset(pcjx_clock_t *clock)
{
    if (clock->nvr.regs[13] & 1)
        pcjx_clock_release(clock);
    clock->hold_dirty = 0;
    clock->pending_second = 0;
}

static void
pcjx_clock_close(pcjx_clock_t *clock)
{
    /* The normal machine teardown saves NVR before device_close_all(). */
    timer_disable(&clock->nvr.onesec_time);
    timer_disable(&clock->periodic_timer);
    timer_disable(&clock->pulse_timer);
    nvr_close();
    free(clock->nvr.fn);
    clock->nvr.fn = NULL;
}

typedef struct pcjx_route_t {
    uint8_t *data;
    uint32_t nominal;
    uint8_t writable;
    uint8_t cartridge;
    uint8_t base_rom;
} pcjx_route_t;

typedef struct pcjx_page_t {
    pcjx_route_t route[14];
    uint8_t count;
    uint8_t shared;
    uint8_t font_read, font_write;
} pcjx_page_t;

typedef struct pcjx_t {
    pcjx_video_t video;
    mem_mapping_t mapping;
    uint8_t memory_reg[11][2];
    uint8_t io_reg[20][2];
    uint8_t decoder_phase, decoder_selector;
    uint8_t pa, pb, ppi_control, nmi_control;
    uint8_t keyboard_level, keyboard_latched;
    uint8_t rom_data[0x20000];
    uint8_t rom_present[32];
    uint8_t dedicated_vram[0x8000];
    uint8_t *font_data; /* Owned CG2 image followed by separate writable gaiji RAM. */
    uint32_t general_size;
    pcjx_page_t pages[256];
    fdc_t *fdc;
    pit_t *pit;
    lpt_t *lpt;
    void *gameport;
    sn76489_t psg;
    uint8_t fdc_alias[128];
    uint8_t clock_decode[16];
    uint8_t io_installed[20];
    uint8_t rtc_enabled;
    pcjx_keyboard_t keyboard;
    pcjx_clock_t clock;
    pc_timer_t cassette_timer;
    int16_t cassette_buffer[SOUNDBUFLEN];
    unsigned cassette_pos;
} pcjx_t;

/* PIT callbacks receive the PIT object, not a motherboard opaque pointer. */
static pcjx_t *pcjx_active;

/* Replay is provisional acquisition evidence, independently of geometry.
 * Set this manifest decision to zero for the absent-socket comparison. */
static const int pcjx_1986_e000_populated = 1;

typedef struct pcjx_rom_file_t {
    const char *name;
    uint32_t offset, size;
    uint8_t profiles;
    uint8_t kanji;
} pcjx_rom_file_t;

static const pcjx_rom_file_t pcjx_rom_files[] = {
    { "64X9708_E000_EMPTY.BIN",           0x00000, 0x08000, 2, 0 },
    { "64X9708_E800_BASIC_JX100.BIN",     0x08000, 0x08000, 3, 0 },
    { "5601JDA_F000_LOW_ROM.BIN",         0x10000, 0x06000, 1, 0 },
    { "5601JDA_F600_BASIC_C120.BIN",      0x16000, 0x08000, 1, 0 },
    { "5601JDA_FE00_ROM_BIOS.BIN",        0x1e000, 0x02000, 1, 0 },
    { "64X9708_F000_LOW_ROM.BIN",         0x10000, 0x06000, 2, 0 },
    { "64X9708_F600_BASIC_C120.BIN",       0x16000, 0x08000, 2, 0 },
    { "64X9708_FE00_ROM_BIOS.BIN",        0x1e000, 0x02000, 2, 0 },
    { "5601_JBA_JFC_E000_IBASIC_102.BIN", 0x00000, 0x18000, 4, 0 },
    { "5601_JBA_JFC_F800_ROM_BIOS.BIN",   0x18000, 0x08000, 4, 0 },
    { "5601_JBA_JFC_KANJI_PATCHED.BIN",   0x00000, PCJX_CG2_IMAGE_SIZE, 4, 1 }
};

/* NULL board is availability-only: no allocations, timers or mappings. */
static int
pcjx_load_roms(pcjx_t *dev, unsigned profile)
{
    char path[256];
    for (unsigned i = 0; i < sizeof(pcjx_rom_files) / sizeof(pcjx_rom_files[0]); i++) {
        const pcjx_rom_file_t *file = &pcjx_rom_files[i];
        if (!(file->profiles & profile) ||
            (i == 0 && !pcjx_1986_e000_populated))
            continue;
        snprintf(path, sizeof(path), "roms/machines/ibmpcjx/%s", file->name);
        FILE *fp = rom_fopen(path, "rb");
        int ok = fp != NULL;
        if (fp) {
            ok = !fseek(fp, 0, SEEK_END) && ftell(fp) == (long) file->size;
            if (ok && dev) {
                uint8_t *dest = file->kanji ? dev->font_data : dev->rom_data + file->offset;
                ok = !fseek(fp, 0, SEEK_SET) && fread(dest, 1, file->size, fp) == file->size;
                if (ok && !file->kanji)
                    memset(dev->rom_present + (file->offset >> 12), 1, file->size >> 12);
            }
            if (fclose(fp))
                ok = 0;
        }
        if (!ok) {
            if (dev || !bios_only)
                pclog("PC JX: unable to load exact ROM resource %s (%u bytes)\n", path, file->size);
            return 0;
        }
    }
    return 1;
}

static int
pcjx_memory_match(const pcjx_t *dev, unsigned selector, uint32_t address, int write)
{
    unsigned r1 = dev->memory_reg[selector][0];
    unsigned r2 = dev->memory_reg[selector][1];
    switch (selector) {
        case 0: /* Fixed memory, ROM read and A19..A17. */
            r1 = (r1 & 0x83) | 0x3c;
            r2 = (r2 & 0x83) | 0x20;
            break;
        case 1:
        case 2:
            r1 |= 0x10;
            r2 &= ~0x10;
            break;
        case 3:
        case 4:
        case 5:
        case 6:
            r1 = (r1 & 0x8f) | 0x30;
            r2 = (r2 & 0x8f) | 0x20;
            break;
        case 7:
            r1 &= ~0x40;
            break;
        case 8:
        case 10:
            r1 = (r1 & ~0x40) | 0x20;
            r2 |= 0x60;
            break;
        case 9:
            r1 |= 0x60;
            r2 |= 0x20;
            break;
    }
    return (r1 & 0x80) && (r1 & 0x20) && (r2 & (write ? 0x40 : 0x20)) &&
           !((((address >> 15) & 0x1f) ^ r1) & ~r2 & 0x1f);
}

static void
pcjx_add_route(pcjx_page_t *page, uint8_t *data, uint32_t nominal,
               int writable, int shared, int cartridge, int base_rom)
{
    pcjx_route_t *route = &page->route[page->count++];
    route->data = data;
    route->nominal = nominal;
    route->writable = writable;
    route->cartridge = cartridge;
    route->base_rom = base_rom;
    page->shared |= shared;
}

static void
pcjx_rebuild_memory(pcjx_t *dev)
{
    unsigned expansion = dev->general_size > 0x20000 ? dev->general_size - 0x20000 : 0;
    unsigned shared_base = (dev->video.jx_array[3] & 0x10) ? 0 : expansion;
    unsigned expansion_base = shared_base ? 0 : 0x20000;
    unsigned pg1 = ((dev->video.memctrl >> 3) & 7) << 14;
    unsigned pg1_mask = (dev->video.memctrl & 0xc0) == 0xc0 ? 0x7fff : 0x3fff;
    unsigned pg2 = ((dev->video.pg2 >> 3) & 3) << 14;
    if (pg1_mask == 0x7fff)
        pg1 &= ~0x4000;
    memset(dev->pages, 0, sizeof(dev->pages));
    for (unsigned p = 0; p < 256; p++) {
        pcjx_page_t *page = &dev->pages[p];
        unsigned address = p << 12;
        if (pcjx_memory_match(dev, 0, address, 0) && dev->rom_present[(address & 0x1ffff) >> 12])
            pcjx_add_route(page, dev->rom_data + (address & 0x1ffff), 0, 0, 0, 0, 1);
        for (unsigned s = 1; s <= 6; s++) {
            if (pcjx_memory_match(dev, s, address, 0))
                pcjx_add_route(page, NULL, 0xd0000 + (s - 1) * 0x8000 + (address & 0x7fff), 0, 0, 1, 0);
        }
        /* Expansion cards have independent fixed bank decoders. In particular
         * they remain probeable while selector 08 disables the shared pair. */
        if (address >= expansion_base && address - expansion_base < expansion)
            pcjx_add_route(page, ram + 0x20000 + address - expansion_base, 0, 1, 0, 0, 0);
        if (pcjx_memory_match(dev, 8, address ^ shared_base, 0))
            pcjx_add_route(page, ram + ((address ^ shared_base) & 0x1ffff), 0, 1, 1, 0, 0);
        if (!(dev->video.array[4] & 3) &&
            pcjx_memory_match(dev, 9, address, 0)) {
            unsigned offset = pg1 + (address & pg1_mask);
            if (offset < 0x20000)
                pcjx_add_route(page, ram + offset, 0, pcjx_memory_match(dev, 9, address, 1), 1, 0, 0);
        }
        if (!(dev->video.array[4] & 3) &&
            pcjx_memory_match(dev, 10, address, 0)) {
            unsigned offset = pg2 + (address & 0x7fff);
            if (offset < sizeof(dev->dedicated_vram))
                pcjx_add_route(page, dev->dedicated_vram + offset, 0, 1, 1, 0, 0);
        }
        /* KJCS is independent of CPU VRAM paging and display font access. */
        if (dev->video.cg2) {
            page->font_read = pcjx_memory_match(dev, 7, address, 0);
            page->font_write = pcjx_memory_match(dev, 7, address, 1);
        }
    }
    mem_mapping_recalc(0, 0x100000, 0);
    flushmmucache();
}

static uint8_t
pcjx_readb(uint32_t address, void *priv)
{
    pcjx_t *dev = (pcjx_t *) priv;
    address &= 0xfffff;
    const pcjx_page_t *page = &dev->pages[address >> 12];
    unsigned low = address & 0xfff;
    uint8_t value = 0xff, base = 0xff;
    int external_rom = 0;
    if (page->shared)
        pcjx_vid_waitstates();
    if (page->font_read)
        value &= pcjx_vid_font_read(&dev->video, address & 0x3ffff);
    for (unsigned i = 0; i < page->count; i++) {
        const pcjx_route_t *route = &page->route[i];
        if (route->cartridge) {
            uint8_t byte;
            if (cart_read_resource(route->nominal + low, &byte)) {
                external_rom = 1;
                value &= byte;
            }
        } else if (route->base_rom)
            base &= route->data[low];
        else
            value &= route->data[low];
    }
    return external_rom ? value : value & base;
}

static void
pcjx_writeb(uint32_t address, uint8_t value, void *priv)
{
    pcjx_t *dev = (pcjx_t *) priv;
    address &= 0xfffff;
    const pcjx_page_t *page = &dev->pages[address >> 12];
    if (page->shared)
        pcjx_vid_waitstates();
    if (page->font_write)
        pcjx_vid_font_write(&dev->video, address & 0x3ffff, value);
    for (unsigned i = 0; i < page->count; i++) {
        const pcjx_route_t *route = &page->route[i];
        if (route->writable)
            route->data[address & 0xfff] = value;
    }
}

static uint16_t
pcjx_readw(uint32_t address, void *priv)
{
    uint16_t value = pcjx_readb(address, priv);
    return value | ((uint16_t) pcjx_readb(address + 1, priv) << 8);
}

static void
pcjx_writew(uint32_t address, uint16_t value, void *priv)
{
    pcjx_writeb(address, value, priv);
    pcjx_writeb(address + 1, value >> 8, priv);
}

static void
pcjx_keyboard_line(void *priv, int level)
{
    pcjx_t *dev = (pcjx_t *) priv;
    if (level && !dev->keyboard_level)
        dev->keyboard_latched = 1;
    dev->keyboard_level = !!level;
    nmi = dev->keyboard_latched && (dev->nmi_control & 0x80);
}

static void
pcjx_cassette_sample(void *priv)
{
    pcjx_t *dev = (pcjx_t *) priv;
    int16_t sample = 0;
    if (cassette && !(dev->pb & 0x18) && (dev->pb & 0x60) == 0x20)
        sample = pc_cas_get_inp(cassette) ? 2048 : -2048;
    while (dev->cassette_pos < (unsigned) sound_pos_global && dev->cassette_pos < SOUNDBUFLEN)
        dev->cassette_buffer[dev->cassette_pos++] = sample;
    timer_advance_u64(&dev->cassette_timer, TIMER_USEC * 1000000 / SOUND_FREQ);
}

static void
pcjx_cassette_mix(int32_t *buffer, uint16_t len, void *priv)
{
    pcjx_t *dev = (pcjx_t *) priv;
    int16_t sample = 0;
    if (cassette && !(dev->pb & 0x18) && (dev->pb & 0x60) == 0x20)
        sample = pc_cas_get_inp(cassette) ? 2048 : -2048;
    while (dev->cassette_pos < len)
        dev->cassette_buffer[dev->cassette_pos++] = sample;
    for (unsigned i = 0; i < len; i++) {
        buffer[2 * i] += dev->cassette_buffer[i];
        buffer[2 * i + 1] += dev->cassette_buffer[i];
    }
    dev->cassette_pos = 0;
}

static uint8_t
pcjx_ppi_read(uint16_t port, void *priv)
{
    pcjx_t *dev = (pcjx_t *) priv;
    switch (port & 3) {
        case 0: return dev->pa;
        case 1: return dev->pb;
        case 2: {
            int cassette_bit = (cassette && !(dev->pb & 0x18)) ? pc_cas_get_inp(cassette) : ppispeakon;
            return dev->keyboard_latched | 0x02 | (dev->fdc ? 0 : 0x04) |
                   (dev->general_size >= 0x20000 ? 0 : 0x08) |
                   (cassette_bit ? 0x10 : 0) | (ppispeakon ? 0x20 : 0) |
                   (dev->keyboard_level ? 0x40 : 0);
        }
        default: return 0xff; /* The PPI control word is write-only. */
    }
}

static void
pcjx_ppi_write(uint16_t port, uint8_t value, void *priv)
{
    pcjx_t *dev = (pcjx_t *) priv;
    switch (port & 3) {
        case 0:
            dev->pa = value;
            break;
        case 1:
            speaker_update();
            dev->pb = value;
            dev->video.pb = value;
            speaker_gated = value & 1;
            speaker_enable = value & 2;
            if (speaker_enable)
                was_speaker_enable = 1;
            /* PB4 disables the internal beeper, not the selected external
             * source. The existing mixer combines both PIT2 routes. */
            speaker_mute = (value & 0x10) && (value & 0x60);
            sn76489_mute = (value & 0x60) != 0x60;
            pit_devs[0].set_gate(pit_devs[0].data, 2, value & 1);
            if (cassette) {
                pc_cas_set_motor(cassette, !(value & 0x18));
                pc_cas_set_out(cassette, ppispeakon);
            }
            pcjx_rebuild_memory(dev);
            break;
        case 3:
            dev->ppi_control = value;
            break;
        default:
            break;
    }
}

static uint8_t
pcjx_nmi_read(uint16_t port, void *priv)
{
    pcjx_t *dev = (pcjx_t *) priv;
    (void) port;
    dev->keyboard_latched = 0;
    nmi = 0;
    return 0;
}

static void
pcjx_nmi_write(uint16_t port, uint8_t value, void *priv)
{
    pcjx_t *dev = (pcjx_t *) priv;
    (void) port;
    dev->nmi_control = value;
    nmi_mask = value & 0x80;
    nmi = dev->keyboard_latched && (value & 0x80);
    pit_devs[0].set_using_timer(pit_devs[0].data, 1, !(value & 0x20));
}

static void
pcjx_pit_irq0(int new_out, int old_out, void *priv)
{
    (void) priv;
    if (new_out && !old_out) {
        picint(1);
        if (pcjx_active && (pcjx_active->nmi_control & 0x20))
            pit_devs[0].ctr_clock(pit_devs[0].data, 1);
    }
    if (!new_out)
        picintc(1);
}

static uint8_t
pcjx_video_read(uint16_t port, void *priv)
{
    pcjx_t *dev = (pcjx_t *) priv;
    uint8_t vp = ((dev->io_reg[12][0] >> 7) & 1) | ((dev->io_reg[13][0] >> 6) & 2);
    return pcjx_vid_in(port, vp, &dev->video);
}

static void
pcjx_video_write(uint16_t port, uint8_t value, void *priv)
{
    pcjx_t *dev = (pcjx_t *) priv;
    uint8_t vp = ((dev->io_reg[12][0] >> 7) & 1) | ((dev->io_reg[13][0] >> 6) & 2);
    int pg1 = dev->video.memctrl;
    uint8_t pg2 = dev->video.pg2;
    uint8_t resources = dev->video.jx_array[1];
    uint8_t order = dev->video.jx_array[3];
    uint8_t reset = dev->video.array[4];
    pcjx_vid_out(port, value, vp, &dev->video);
    if (pg1 != dev->video.memctrl || pg2 != dev->video.pg2 ||
        ((resources ^ dev->video.jx_array[1]) & 0x30) ||
        ((order ^ dev->video.jx_array[3]) & 0x10) ||
        ((reset ^ dev->video.array[4]) & 3))
        pcjx_rebuild_memory(dev);
}

static void
pcjx_video_handler(pcjx_t *dev, int set, uint16_t port, uint16_t size)
{
    io_handler(set, port, size, pcjx_video_read, NULL, NULL, pcjx_video_write, NULL, NULL, dev);
}

static void
pcjx_remove_io(pcjx_t *dev)
{
    if (dev->io_installed[0])
        pic_handler(0, 0x20, 8);
    if (dev->io_installed[1])
        pit_handler(0, 0x40, 8, dev->pit);
    if (dev->io_installed[2])
        io_removehandler(0x60, 8, pcjx_ppi_read, NULL, NULL, pcjx_ppi_write, NULL, NULL, dev);
    if (dev->io_installed[3])
        io_removehandler(0xa0, 8, pcjx_nmi_read, NULL, NULL, pcjx_nmi_write, NULL, NULL, dev);
    if (dev->io_installed[4])
        io_removehandler(0xc0, 8, NULL, NULL, NULL, sn76489_write, NULL, NULL, &dev->psg);
    if (dev->fdc) {
        for (unsigned i = 0; i < 128; i++) {
            if (dev->fdc_alias[i]) {
                dev->fdc->base_address = i << 3;
                fdc_remove(dev->fdc);
                dev->fdc_alias[i] = 0;
            }
        }
    }
    gameport_set_decode(dev->gameport, 0, 0);
    if (dev->lpt)
        lpt_port_remove(dev->lpt);
    if (dev->io_installed[10])
        pcjx_video_handler(dev, 0, 0x3d0, 8);
    if (dev->io_installed[12] || dev->io_installed[13] || dev->io_installed[15])
        pcjx_video_handler(dev, 0, 0x3da, 1);
    if (dev->io_installed[15]) {
        pcjx_video_handler(dev, 0, 0x3db, 2);
        pcjx_video_handler(dev, 0, 0x3de, 1);
    }
    if (dev->io_installed[16])
        pcjx_video_handler(dev, 0, 0x3d9, 1);
    if (dev->io_installed[17])
        pcjx_video_handler(dev, 0, 0x3df, 1);
    for (unsigned i = 0; i < 16; i++) {
        if (dev->clock_decode[i])
            io_removehandler(0x360 + i, 1, pcjx_clock_read, NULL, NULL, pcjx_clock_write, NULL, NULL, &dev->clock);
        dev->clock_decode[i] = 0;
    }
    memset(dev->io_installed, 0, sizeof(dev->io_installed));
}

static void
pcjx_rebuild_io(pcjx_t *dev)
{
    pcjx_remove_io(dev);
    for (unsigned i = 0; i < 20; i++)
        dev->io_installed[i] = !!(dev->io_reg[i][0] & 0x80);
    const uint8_t *on = dev->io_installed;
    if (on[0]) pic_handler(1, 0x20, 8);
    if (on[1]) pit_handler(1, 0x40, 8, dev->pit);
    if (on[2]) io_sethandler(0x60, 8, pcjx_ppi_read, NULL, NULL, pcjx_ppi_write, NULL, NULL, dev);
    if (on[3]) io_sethandler(0xa0, 8, pcjx_nmi_read, NULL, NULL, pcjx_nmi_write, NULL, NULL, dev);
    if (on[4]) io_sethandler(0xc0, 8, NULL, NULL, NULL, sn76489_write, NULL, NULL, &dev->psg);
    if (on[5] && dev->fdc) {
        for (unsigned i = 0; i < 128; i++) {
            if (!((i ^ dev->io_reg[5][0]) & ~dev->io_reg[5][1] & 0x7f)) {
                fdc_set_base(dev->fdc, i << 3);
                dev->fdc_alias[i] = 1;
            }
        }
    }
    gameport_set_decode(dev->gameport, on[7], on[6]);
    if (on[8] && dev->lpt)
        lpt_port_setup(dev->lpt, 0x378);
    /* Serial, GA01, extension video and modem are unpopulated. */
    if (on[10]) pcjx_video_handler(dev, 1, 0x3d0, 8);
    if (on[12] || on[13] || on[15]) pcjx_video_handler(dev, 1, 0x3da, 1);
    if (on[15]) {
        pcjx_video_handler(dev, 1, 0x3db, 2);
        pcjx_video_handler(dev, 1, 0x3de, 1);
    }
    if (on[16]) pcjx_video_handler(dev, 1, 0x3d9, 1);
    if (on[17]) pcjx_video_handler(dev, 1, 0x3df, 1);
    /* The clock is on the external path. An internally selected FDC
     * block isolates it, even when both devices' addresses overlap. */
    if (dev->rtc_enabled && on[19]) {
        for (unsigned i = 0; i < 16; i++) {
            unsigned block = (0x360 + i) >> 3;
            int internal = on[5] && !((block ^ dev->io_reg[5][0]) & ~dev->io_reg[5][1] & 0x7f);
            if (!internal) {
                io_sethandler(0x360 + i, 1, pcjx_clock_read, NULL, NULL, pcjx_clock_write, NULL, NULL, &dev->clock);
                dev->clock_decode[i] = 1;
            }
        }
    }
}

static uint8_t
pcjx_decoder_read(uint16_t port, void *priv)
{
    pcjx_t *dev = (pcjx_t *) priv;
    (void) port;
    dev->decoder_phase = 0;
    return 0xff;
}

static void
pcjx_decoder_write(uint16_t port, uint8_t value, void *priv)
{
    pcjx_t *dev = (pcjx_t *) priv;
    (void) port;
    if (dev->decoder_phase == 0) {
        dev->decoder_selector = value;
        dev->decoder_phase = 1;
    } else {
        unsigned selector = dev->decoder_selector;
        unsigned reg = dev->decoder_phase - 1;
        dev->decoder_phase = reg ? 0 : 2;
        /* REG1 takes effect without a following REG2 write. Native BIOS
         * toggles resources this way, retaining their programmed masks. */
        if (selector <= 0x0a) {
            dev->memory_reg[selector][reg] = value;
            pcjx_rebuild_memory(dev);
        } else if (selector >= 0x80 && selector <= 0x93) {
            dev->io_reg[selector - 0x80][reg] = value;
            pcjx_rebuild_io(dev);
        }
    }
}

static void
pcjx_reset(void *priv)
{
    pcjx_t *dev = (pcjx_t *) priv;
    pcjx_keyboard_reset(&dev->keyboard);
    dev->keyboard_latched = 0;
    nmi = 0;
    if (dev->rtc_enabled)
        pcjx_clock_warm_reset(&dev->clock);
    /* Hardware registers and RAM survive CPU resets. */
}


static void
pcjx_close(void *priv)
{
    pcjx_t *dev = (pcjx_t *) priv;
    pcjx_remove_io(dev);
    io_removehandler(0x1ff, 1, pcjx_decoder_read, NULL, NULL, pcjx_decoder_write, NULL, NULL, dev);
    timer_disable(&dev->video.timer);
    timer_disable(&dev->cassette_timer);
    pcjx_keyboard_close(&dev->keyboard);
    if (dev->rtc_enabled)
        pcjx_clock_close(&dev->clock);
    mem_mapping_disable(&dev->mapping);
    picintc(1 << 5);
    pcjx_active = NULL;
    nmi = 0;
    free(dev->font_data);
    free(dev);
}

static const device_config_t pcjx_config[] = {
    {
        .name = "bios",
        .description = "BIOS Version",
        .type = CONFIG_BIOS,
        .default_string = "1986",
        .bios = {
            {
                .name = "1985 English (5601JDA, 360KB BIOS)",
                .internal_name = "1985",
                .bios_type = BIOS_NORMAL,
                .files_no = 4,
                .local = 1,
                .files = {
                    "roms/machines/ibmpcjx/64X9708_E800_BASIC_JX100.BIN",
                    "roms/machines/ibmpcjx/5601JDA_F000_LOW_ROM.BIN",
                    "roms/machines/ibmpcjx/5601JDA_F600_BASIC_C120.BIN",
                    "roms/machines/ibmpcjx/5601JDA_FE00_ROM_BIOS.BIN"
                }
            },
            {
                .name = "1986 English (64X9708, 720KB BIOS)",
                .internal_name = "1986",
                .bios_type = BIOS_NORMAL,
                .files_no = 5,
                .local = 2,
                .files = {
                    "roms/machines/ibmpcjx/64X9708_E000_EMPTY.BIN",
                    "roms/machines/ibmpcjx/64X9708_E800_BASIC_JX100.BIN",
                    "roms/machines/ibmpcjx/64X9708_F000_LOW_ROM.BIN",
                    "roms/machines/ibmpcjx/64X9708_F600_BASIC_C120.BIN",
                    "roms/machines/ibmpcjx/64X9708_FE00_ROM_BIOS.BIN"
                }
            },
            {
                .name = "Japanese (5601JBA/JFC, experimental)",
                .internal_name = "japanese",
                .bios_type = BIOS_NORMAL,
                .files_no = 3,
                .local = 4,
                .files = {
                    "roms/machines/ibmpcjx/5601_JBA_JFC_E000_IBASIC_102.BIN",
                    "roms/machines/ibmpcjx/5601_JBA_JFC_F800_ROM_BIOS.BIN",
                    "roms/machines/ibmpcjx/5601_JBA_JFC_KANJI_PATCHED.BIN"
                }
            },
            { .files_no = 0 }
        }
    },
    { .name = "rtc_enabled", .description = "Clock option", .type = CONFIG_BINARY, .default_int = 1 },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t pcjx_device = {
    .name = "IBM PC JX",
    .internal_name = "pcjx",
    .flags = DEVICE_KBC | DEVICE_SOFTRESET,
    .close = pcjx_close,
    .reset = pcjx_reset,
    .config = pcjx_config
};

static int
pcjx_init(const machine_t *model, unsigned profile)
{
    if (!pcjx_load_roms(NULL, profile))
        return 0;
    if (bios_only)
        return 1;
    pcjx_t *dev = (pcjx_t *) calloc(1, sizeof(pcjx_t));
    if (!dev)
        return 0;
    memset(dev->rom_data, 0xff, sizeof(dev->rom_data));
    /* Native video and its font hardware are fixed by the Japanese BIOS profile. */
    if (profile == 4) {
        dev->font_data = (uint8_t *) malloc(PCJX_CG2_IMAGE_SIZE + PCJX_GAIJI_SIZE);
        if (!dev->font_data) {
            free(dev);
            return 0;
        }
        memset(dev->font_data + PCJX_CG2_IMAGE_SIZE, 0, PCJX_GAIJI_SIZE);
    }
    if (!pcjx_load_roms(dev, profile)) {
        free(dev->font_data);
        free(dev);
        return 0;
    }
    dev->general_size = mem_size << 10;
    device_context(model->device);
    dev->rtc_enabled = machine_get_config_int("rtc_enabled");
    device_context_restore();
    pcjx_active = dev;
    pic_init_pcjr();
    pic_handler(0, 0x20, 8);
    dev->pit = pit_common_init(PIT_8253, pcjx_pit_irq0, NULL);
    pit_handler(0, 0x40, 4, dev->pit);
    video_reset(gfxcard[0]);
    pcjx_vid_init(&dev->video, ram, 0x20000, dev->dedicated_vram, NULL,
                  dev->font_data, dev->font_data ? dev->font_data + PCJX_CG2_IMAGE_SIZE : NULL);
    device_add_ex(&pcjx_video_device, &dev->video);
    /* The PCjr font asset is a CG1 compatibility approximation, not a JX dump. */
    dev->video.jx_array[3] |= 0x10;
    sn76489_init(&dev->psg, 0, 0, SN76496, 3579545);
    if (fdc_current[0] == FDC_INTERNAL) {
        dev->fdc = (fdc_t *) device_add(&fdc_pcjx_device);
        fdc_remove(dev->fdc);
    }
    dev->gameport = gameport_add(&gameport_200_device);
    gameport_set_decode(dev->gameport, 0, 0);
    dev->lpt = (lpt_t *) device_add(&lpt_port_device);
    lpt_port_remove(dev->lpt);
    lpt_set_next_inst(PARALLEL_MAX - 1);
    serial_set_next_inst(SERIAL_MAX - 1);
    /* No DMA, no serial card and no PCjr sidecar identity. */
    nmi = nmi_mask = 0;
    pcjx_keyboard_init(&dev->keyboard, pcjx_keyboard_line, dev);
    if (dev->rtc_enabled)
        pcjx_clock_init(&dev->clock);
    sound_add_handler(pcjx_cassette_mix, dev);
    timer_add(&dev->cassette_timer, pcjx_cassette_sample, dev, 1);
    mem_mapping_disable(&ram_low_mapping);
    mem_mapping_disable(&ram_mid_mapping);
    mem_mapping_disable(&ram_high_mapping);
    mem_mapping_disable(&ram_remapped_mapping);
    mem_mapping_disable(&bios_mapping);
    mem_mapping_disable(&bios_high_mapping);
    mem_set_mem_state(0, 0x100000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
    /* No direct execution pointer: 8088 prefetch follows readb and incurs
     * exactly the same shared-memory contention as ordinary data cycles. */
    mem_mapping_add(&dev->mapping, 0, 0x100000, pcjx_readb, pcjx_readw, NULL,
                    pcjx_writeb, pcjx_writew, NULL, NULL, MEM_MAPPING_EXTERNAL, dev);
    dev->memory_reg[0][0] = 0xbc;
    dev->memory_reg[0][1] = 0x23;
    pcjx_rebuild_memory(dev);
    io_sethandler(0x1ff, 1, pcjx_decoder_read, NULL, NULL, pcjx_decoder_write, NULL, NULL, dev);
    /* Register last: close removes child-device handlers before they are freed. */
    device_add_ex(&pcjx_device, dev);
    return 1;
}

int
machine_pcjx_init(const machine_t *model)
{
    device_context(model->device);
    unsigned profile = device_get_bios_local(model->device, device_get_config_bios("bios"));
    device_context_restore();
    return profile ? pcjx_init(model, profile) : 0;
}

int
machine_is_pcjx(int m)
{
    return machines[m].init == machine_pcjx_init;
}
