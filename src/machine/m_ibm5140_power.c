/*
 * 86Box: IBM PC Convertible system-board power, MC146818A and keyboard.
 *
 * The ROM owns translation, suspend shadows and resume validation. This device
 * retains physical SRAM, never CPU state or synthesized BIOS data structures.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/keyboard.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/dma.h>
#include <86box/m_ibm5140.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/snd_speaker.h>
#include <86box/softpower.h>
#include <86box/ibm5140_power.h>

#define NMI_KEY_DATA  0x01
#define NMI_RTC       0x04
#define NMI_SUSPEND   0x08
#define NMI_KEY_CLEAR 0x10
#define NMI_CHECK     0x40
#define NMI_FDC       0x80
#define RTC_A         0x0a
#define RTC_B         0x0b
#define RTC_C         0x0c
#define RTC_D         0x0d
#define RTC_SET       0x80
#define RTC_PF        0x40
#define RTC_AF        0x20
#define RTC_UF        0x10

struct ibm5140_power_t {
    ibm5140_video_t *video;
    softpower_control_t *control;
    nvr_t rtc;
    pc_timer_t rtc_update, rtc_periodic, scanner, clear_timer;
    uint64_t periodic_delay;
    double periodic_usec;
    uint32_t update_usec;
    uint8_t port61, port72, port7c, porta0, power_status, interrupt_simulation;
    uint8_t causes, nmi_line, rtc_index, rtc_uip, rtc_phase, dst_fell_back;
    uint8_t raw, compatible, clear_pending;
    uint8_t pit_control[2], pit_select;
    uint8_t switches[0x60], diagnostic[0x60], scanned[0x60], debounce[0x60];
    uint8_t host_down[0x200], owners[0x60];
    uint8_t repeat_key, reset_chord_down;
    unsigned repeat_ms;
    int powered_off, retention_valid, qualified;
};

static ibm5140_power_t *active;

/* Interrupt/DMA wake latches DISABLE_SLEEP, even while the run-clock bits
 * are still set. Port 72h and its hardware latch have a single owner. */
void
ibm5140_clock_wake(void)
{
    if (!active || active->powered_off)
        return;
    active->port72 |= 4;
    cpu_clock_gated = 0;
}

static int
ibm5140_wake_pending(void)
{
    return pic.int_pending || (nmi && nmi_mask && nmi_enable) ||
           dma_get_drq(1) || dma_get_drq(2) || dma_get_drq(3);
}

static void
ibm5140_update_clock(ibm5140_power_t *dev)
{
    pic_ibm5140_diag_enable(!dev->powered_off && (dev->port72 & 0x80));
    cpu_clock_gated = dev->powered_off || !(dev->port72 & 7);
    /* Close the check-then-sleep race with a request already asserted. */
    if (!dev->powered_off && ibm5140_wake_pending())
        ibm5140_clock_wake();
}

/* Board-supplied query for the 80C88's stoppable clock; see cpu.c. Answering
 * may wake the clock, so the core only asks while the clock is gated. */
static int
ibm5140_cpu_clock_stopped(void)
{
    if (!active)
        return 0;
    if (active->powered_off)
        return 1;
    if (ibm5140_wake_pending())
        ibm5140_clock_wake();
    return cpu_clock_gated;
}

/* Set-1 host physical identities, not PC scan bytes delivered to the guest.
 * Right Ctrl is the explicit Fn binding; right Alt remains regional AltGr.
 * 73h/7Dh expose the extra non-US key, 56h the ISO left-Shift neighbour.
 */
static const uint8_t host_raw[0x200] = {
    [0x001] = 0x01, [0x002] = 0x12, [0x003] = 0x13, [0x004] = 0x14,
    [0x005] = 0x15, [0x006] = 0x16, [0x007] = 0x17, [0x008] = 0x18,
    [0x009] = 0x19, [0x00a] = 0x1a, [0x00b] = 0x1b, [0x00c] = 0x1c,
    [0x00d] = 0x1d, [0x00e] = 0x1f, [0x00f] = 0x21, [0x010] = 0x22,
    [0x011] = 0x23, [0x012] = 0x24, [0x013] = 0x25, [0x014] = 0x26,
    [0x015] = 0x27, [0x016] = 0x28, [0x017] = 0x29, [0x018] = 0x2a,
    [0x019] = 0x2b, [0x01a] = 0x2c, [0x01b] = 0x2d, [0x01c] = 0x3e,
    [0x01d] = 0x51, [0x01e] = 0x32, [0x01f] = 0x33, [0x020] = 0x34,
    [0x021] = 0x35, [0x022] = 0x36, [0x023] = 0x37, [0x024] = 0x38,
    [0x025] = 0x39, [0x026] = 0x3a, [0x027] = 0x3b, [0x028] = 0x3c,
    [0x029] = 0x11, [0x02a] = 0x41, [0x02b] = 0x1e, [0x02c] = 0x42,
    [0x02d] = 0x43, [0x02e] = 0x44, [0x02f] = 0x45, [0x030] = 0x46,
    [0x031] = 0x47, [0x032] = 0x48, [0x033] = 0x49, [0x034] = 0x4a,
    [0x035] = 0x4b, [0x036] = 0x4c, [0x037] = 0x4e, [0x038] = 0x53,
    [0x039] = 0x56, [0x03a] = 0x31, [0x03b] = 0x02, [0x03c] = 0x03,
    [0x03d] = 0x04, [0x03e] = 0x05, [0x03f] = 0x06, [0x040] = 0x07,
    [0x041] = 0x08, [0x042] = 0x09, [0x043] = 0x0a, [0x044] = 0x0b,
    [0x045] = 0x0c, [0x046] = 0x0d, [0x056] = 0x54, [0x073] = 0x3d,
    [0x07d] = 0x3d, [0x11d] = 0x52, [0x137] = 0x4e, [0x138] = 0x5a,
    [0x148] = 0x5c, [0x14b] = 0x5b, [0x14d] = 0x5f, [0x150] = 0x5e,
    [0x152] = 0x0e, [0x153] = 0x0f
};

static uint8_t
ibm5140_sources(const ibm5140_power_t *dev)
{
    uint8_t sources = dev->causes;

    if ((dev->port72 & 0x80) && (dev->interrupt_simulation & 1))
        sources |= NMI_CHECK;

    /* Do not let a simultaneous RTC/data NMI expose clear before IRQ1 EOI. */
    if (dev->clear_pending && !(pic.isr & 2))
        sources |= NMI_KEY_CLEAR;
    return sources;
}

static void
ibm5140_update_nmi(ibm5140_power_t *dev)
{
    uint8_t sources = ibm5140_sources(dev);
    int line;

    if (!(dev->port7c & 0x80))
        sources &= ~(NMI_KEY_DATA | NMI_KEY_CLEAR);
    if (dev->port61 & 8)
        sources &= ~NMI_RTC;
    if (!(dev->porta0 & 0x80) || (dev->port61 & 0x20))
        sources &= ~NMI_CHECK;
    if (!(softpower_control_read(dev->control) & 4))
        sources &= ~NMI_SUSPEND;
    line = !dev->powered_off && (dev->port72 & 0x20) && sources;
    if (line && !dev->nmi_line) {
        ibm5140_clock_wake();
        nmi_raise();
    }
    dev->nmi_line = !!line;
}

void
ibm5140_power_nmi(ibm5140_power_t *dev, uint8_t cause, int asserted)
{
    if (!dev)
        return;
    cause &= NMI_KEY_DATA | NMI_RTC | NMI_SUSPEND | NMI_CHECK | NMI_FDC;
    if (asserted)
        dev->causes |= cause;
    else
        dev->causes &= ~cause;
    ibm5140_update_nmi(dev);
}

static void
ibm5140_rtc_irq(ibm5140_power_t *dev)
{
    uint8_t *r = dev->rtc.regs;
    int asserted = !!(r[RTC_B] & r[RTC_C] & 0x70);

    r[RTC_C] = (r[RTC_C] & 0x70) | (asserted ? 0x80 : 0);
    ibm5140_power_nmi(dev, NMI_RTC, asserted);
}

static unsigned
rtc_decode(const ibm5140_power_t *dev, uint8_t value)
{
    return (dev->rtc.regs[RTC_B] & 4) ? value : RTC_DCB(value);
}

static uint8_t
rtc_encode(const ibm5140_power_t *dev, unsigned value)
{
    return (dev->rtc.regs[RTC_B] & 4) ? value : RTC_BCD(value);
}

/* The century location is battery-backed RAM, not a clock counter. */
static void
ibm5140_rtc_second(ibm5140_power_t *dev)
{
    uint8_t *r = dev->rtc.regs;
    unsigned sec = rtc_decode(dev, r[0]);
    unsigned min = rtc_decode(dev, r[2]);
    unsigned hour = rtc_decode(dev, r[4] & 0x7f);
    unsigned weekday = rtc_decode(dev, r[6]);
    unsigned day = rtc_decode(dev, r[7]);
    unsigned month = rtc_decode(dev, r[8]);
    unsigned year = rtc_decode(dev, r[9]);

    if (!(r[RTC_B] & 2))
        hour = (hour % 12) + ((r[4] & 0x80) ? 12 : 0);
    /* Invalid programmed calendar fields remain guest-visible until rollover;
       never index the month table using uninitialized CMOS RAM. */
    if (++sec >= 60) {
        sec = 0;
        if (++min >= 60) {
            min = 0;
            if (++hour == 2 && (r[RTC_B] & 1) && weekday == 1 && day >= 24) {
                if (month == 4)
                    hour = 3;
                else if (month == 10 && day >= 25 && !dev->dst_fell_back) {
                    hour = 1;
                    dev->dst_fell_back = 1;
                }
            }
            if (hour >= 24) {
                hour = 0;
                dev->dst_fell_back = 0;
                weekday = weekday % 7 + 1;
                /* The chip's two-digit year counter uses a four-year cycle. */
                if (++day > (unsigned) nvr_get_days((month >= 1 && month <= 12) ? month : 1,
                                                    2000 + year)) {
                    day = 1;
                    if (++month > 12) {
                        month = 1;
                        year = (year + 1) % 100;
                    }
                }
            }
        }
    }
    r[0] = rtc_encode(dev, sec);
    r[2] = rtc_encode(dev, min);
    r[4] = (r[RTC_B] & 2) ? rtc_encode(dev, hour)
                          : rtc_encode(dev, (hour % 12) ? hour % 12 : 12) | (hour >= 12 ? 0x80 : 0);
    r[6] = rtc_encode(dev, weekday);
    r[7] = rtc_encode(dev, day);
    r[8] = rtc_encode(dev, month);
    r[9] = rtc_encode(dev, year);
    nvr_dosave = 1;
}

static void
ibm5140_rtc_update(void *priv)
{
    ibm5140_power_t *dev = priv;
    uint8_t *r = dev->rtc.regs;

    if (!dev->rtc_phase) {
        dev->rtc_uip = (r[RTC_B] & RTC_SET) ? 0 : 0x80;
        dev->rtc_phase = 1;
        /* Same MC146818A transfer timing as the existing RTC component:
           244 us advance warning, followed by the 1984 us update window. */
        timer_advance_u64(&dev->rtc_update, 244ULL * TIMER_USEC);
        return;
    }
    if (dev->rtc_phase == 2) {
        dev->rtc_phase = dev->rtc_uip = 0;
        if (!(r[RTC_B] & RTC_SET)) {
            r[RTC_C] |= RTC_UF;
            ibm5140_rtc_irq(dev);
        }
        timer_on_auto(&dev->rtc_update, dev->update_usec - 2228.0);
        return;
    }
    if (!(r[RTC_B] & RTC_SET) && dev->rtc_uip) {
        int alarm = 1;

        ibm5140_rtc_second(dev);
        for (unsigned i = 0; i < 6; i += 2)
            if (r[i + 1] != r[i] && (r[i + 1] & 0xc0) != 0xc0)
                alarm = 0;
        if (alarm)
            r[RTC_C] |= RTC_AF;
        ibm5140_rtc_irq(dev);
        dev->rtc_phase = 2;
        timer_advance_u64(&dev->rtc_update, 1984ULL * TIMER_USEC);
    } else {
        dev->rtc_phase = dev->rtc_uip = 0;
        timer_on_auto(&dev->rtc_update, dev->update_usec - 244.0);
    }
}

static void
ibm5140_rtc_periodic(void *priv)
{
    ibm5140_power_t *dev = priv;

    dev->rtc.regs[RTC_C] |= RTC_PF;
    ibm5140_rtc_irq(dev);
    if (dev->periodic_delay)
        timer_advance_u64(&dev->rtc_periodic, dev->periodic_delay);
    else
        timer_on_auto(&dev->rtc_periodic, dev->periodic_usec);
}

static void
ibm5140_rtc_retime(ibm5140_power_t *dev, int divider_changed)
{
    unsigned divider = (dev->rtc.regs[RTC_A] >> 4) & 7;
    unsigned rate = dev->rtc.regs[RTC_A] & 15;
    unsigned bypass, shift;

    timer_stop(&dev->rtc_periodic);
    if (divider_changed) {
        timer_stop(&dev->rtc_update);
        dev->rtc_phase = dev->rtc_uip = 0;
    }
    /* The board has a fixed 32.768 kHz crystal. DV=0/1 therefore slow the
       22-stage chain rather than magically replacing that crystal. DV=6/7
       holds the divider in reset; the other encodings are chip test modes. */
    if (divider > 2)
        return;
    bypass = divider == 0 ? 0 : divider == 1 ? 2 : 7;
    dev->update_usec = 1000000U << (7 - bypass);
    if (!timer_is_enabled(&dev->rtc_update))
        timer_on_auto(&dev->rtc_update, dev->update_usec / 2.0 - 244.0);
    if (!rate)
        return;
    shift = rate + 6 - bypass;
    if (shift <= 1)
        shift += 7;
    dev->periodic_usec = (double) (1U << shift) * 1000000.0 / 32768.0;
    dev->periodic_delay = dev->periodic_usec <= MAX_USEC
        ? (uint64_t) (((uint128_t) 1000000 * TIMER_USEC << shift) / 32768) : 0;
    if (dev->periodic_delay)
        timer_set_delay_u64(&dev->rtc_periodic, dev->periodic_delay);
    else
        timer_on_auto(&dev->rtc_periodic, dev->periodic_usec);
}

static void
ibm5140_rtc_defaults(nvr_t *rtc)
{
    rtc->regs[RTC_A] = 0x26;
    rtc->regs[RTC_B] = 2;
    rtc->regs[RTC_D] = 0x80; /* Healthy RTC supply, independent of RAM contents. */
    rtc->regs[6] = 3;
    rtc->regs[7] = 1;
    rtc->regs[8] = 1;
    rtc->regs[9] = 0x80;
    rtc->regs[0x32] = 0x19;
}

static void
ibm5140_rtc_start(nvr_t *rtc)
{
    ibm5140_power_t *dev = rtc->data;

    rtc->regs[RTC_A] &= 0x7f;
    rtc->regs[RTC_B] &= ~0x78;
    rtc->regs[RTC_C] = 0;
    rtc->regs[RTC_D] &= 0x80;
    if (time_sync & TIME_SYNC_ENABLED) {
        struct tm now;

        nvr_time_get(&now);
        rtc->regs[0] = rtc_encode(dev, now.tm_sec);
        rtc->regs[2] = rtc_encode(dev, now.tm_min);
        rtc->regs[4] = (rtc->regs[RTC_B] & 2) ? rtc_encode(dev, now.tm_hour)
            : rtc_encode(dev, now.tm_hour % 12 ? now.tm_hour % 12 : 12) | (now.tm_hour >= 12 ? 0x80 : 0);
        rtc->regs[6] = rtc_encode(dev, now.tm_wday + 1);
        rtc->regs[7] = rtc_encode(dev, now.tm_mday);
        rtc->regs[8] = rtc_encode(dev, now.tm_mon + 1);
        rtc->regs[9] = rtc_encode(dev, (now.tm_year + 1900) % 100);
        rtc->regs[0x32] = rtc_encode(dev, (now.tm_year + 1900) / 100);
    }
}

static void
ibm5140_clear_poll(void *priv)
{
    ibm5140_power_t *dev = priv;

    if (!dev->clear_pending)
        return;
    if (pic.isr & 2)
        timer_advance_u64(&dev->clear_timer, TIMER_USEC);
    else
        ibm5140_update_nmi(dev);
}

static void
ibm5140_keyboard_reset(ibm5140_power_t *dev)
{
    memset(dev->diagnostic, 0, sizeof(dev->diagnostic));
    memset(dev->scanned, 0, sizeof(dev->scanned));
    memset(dev->debounce, 0, sizeof(dev->debounce));
    dev->repeat_key = dev->repeat_ms = 0;
    dev->raw = dev->compatible = 0;
    dev->clear_pending = 0;
    dev->causes &= ~(NMI_KEY_DATA | NMI_KEY_CLEAR);
    timer_disable(&dev->clear_timer);
    picintc(2);
}

static void
ibm5140_board_reset(ibm5140_power_t *dev)
{
    ibm5140_chipset_reset();
    softpower_control_reset(dev->control);
    ibm5140_keyboard_reset(dev);
    ibm5140_video_reset(dev->video);
    pit_device_reset(pit_devs[0].data);
    pit_devs[0].set_gate(pit_devs[0].data, 0, 1);
    pit_devs[0].set_using_timer(pit_devs[0].data, 1, 0);
    pit_devs[0].set_out_func(pit_devs[0].data, 1, NULL);
    dev->pit_select = dev->pit_control[0] = dev->pit_control[1] = 0;
    dev->port61 = 0x28;
    dev->port72 = 7;
    dev->port7c = dev->porta0 = dev->interrupt_simulation = 0;
    dev->causes = dev->nmi_line = 0;
    dev->qualified = 0;
    dev->powered_off = 0;
    dev->rtc.regs[RTC_B] &= ~0x78; /* RTC RESET leaves clock/calendar and SET intact. */
    dev->rtc.regs[RTC_C] = 0;
    nmi = 0;
    nmi_mask = 0x80;
    speaker_update();
    speaker_mute = 1;
    speaker_gated = speaker_enable = was_speaker_enable = 0;
    pit_devs[0].set_gate(pit_devs[0].data, 2, 0);
    ibm5140_update_clock(dev);
    ibm5140_feature_control(dev->port7c);
}

static void
ibm5140_cpu_reset(void *priv)
{
    ibm5140_power_t *dev = priv;

    ibm5140_board_reset(dev);
    dev->power_status = (dev->power_status & 0xc0) | 0x20;
    nvr_dosave = 1;
    softresetx86();
}

static void
ibm5140_scan(void *priv)
{
    ibm5140_power_t *dev = priv;
    const uint8_t *switches = (dev->port7c & 0x20) ? dev->diagnostic : dev->switches;

    timer_advance_u64(&dev->scanner, 1000ULL * TIMER_USEC);
    if (dev->powered_off)
        return;
    if (dev->repeat_ms)
        dev->repeat_ms--;
    /* One hardware latch: a complete down/up while stalled is not queued. */
    if (dev->causes & NMI_KEY_DATA)
        return;
    for (unsigned key = 1; key < 0x60; key++) {
        if (switches[key] == dev->scanned[key]) {
            dev->debounce[key] = 0;
            continue;
        }
        /* The first sample starts debounce; five complete 1 ms intervals
           must follow it, rather than counting a partial host-event interval. */
        if (++dev->debounce[key] < 6)
            continue;
        dev->debounce[key] = 0;
        dev->scanned[key] = switches[key];
        dev->raw = key | (dev->scanned[key] ? 0 : 0x80);
        if (dev->scanned[key]) {
            dev->repeat_key = key;
            dev->repeat_ms = 500;
        } else if (dev->repeat_key == key) {
            dev->repeat_key = dev->repeat_ms = 0;
        }
        if (!(dev->port7c & 0x20) && !dev->reset_chord_down && dev->scanned[0x51] &&
            dev->scanned[0x52] && dev->scanned[0x0f]) {
            dev->reset_chord_down = 1;
            ibm5140_cpu_reset(dev); /* Physical Ctrl+Fn+Del, not suspend. */
            return;
        }
        ibm5140_power_nmi(dev, NMI_KEY_DATA, 1);
        return;
    }
    if (dev->repeat_key && !dev->repeat_ms && switches[dev->repeat_key]) {
        dev->raw = dev->repeat_key;
        dev->repeat_ms = 100;
        ibm5140_power_nmi(dev, NMI_KEY_DATA, 1);
    }
}

static void
ibm5140_host_key(uint16_t scan, int down, void *priv)
{
    ibm5140_power_t *dev = priv;
    uint8_t key;

    if (scan >= 0x200)
        return;
    key = host_raw[scan];
    if (!key || dev->host_down[scan] == !!down)
        return;
    dev->host_down[scan] = !!down;
    if (down)
        dev->owners[key]++;
    else if (dev->owners[key])
        dev->owners[key]--;
    dev->switches[key] = !!dev->owners[key];
    if (!dev->switches[0x51] || !dev->switches[0x52] || !dev->switches[0x0f])
        dev->reset_chord_down = 0;
}

static int
ibm5140_raw_defined(unsigned key)
{
    if (key >= 0x60)
        return 0;
    switch (key) {
        case 0x00: case 0x10: case 0x20: case 0x2e: case 0x2f:
        case 0x30: case 0x3f: case 0x40: case 0x4d: case 0x4f:
        case 0x50: case 0x55: case 0x57: case 0x58: case 0x59: case 0x5d:
            return 0;
        default:
            return 1;
    }
}

/* Appended to the normal 64-byte RTC .nvr image: "IBM5140R", LE32 version=1,
 * LE32 RAM byte count, installed main SRAM, then the video SRAM/font payload.
 * The vendor-save hook runs after every normal NVR rewrite, so it must append
 * the payload on every qualified save. Commit the magic last. The BIOS, not
 * a host CPU snapshot or synthesized signature/checksum, owns resume validation.
 */
static void
ibm5140_retention_save(void)
{
    ibm5140_power_t *dev = active;
    uint32_t bytes = mem_size * 1024;
    uint8_t header[16] = {0};
    FILE *fp;
    int ok;

    /* nvr_save() has already rewritten the RTC prefix and removed any old tail. */
    if (!dev || !dev->qualified || !dev->retention_valid)
        return;
    fp = nvr_fopen(dev->rtc.fn, "r+b");
    if (!fp) {
        pclog("IBM5140: cannot append retained SRAM to NVR\n");
        return;
    }
    ok = !fseek(fp, dev->rtc.size, SEEK_SET) &&
         fwrite(header, sizeof(header), 1, fp) == 1 &&
         fwrite(ram, bytes, 1, fp) == 1 && ibm5140_video_save(dev->video, fp);
    memcpy(header, "IBM5140R", 8);
    header[8] = 1;
    for (unsigned i = 0; i < 4; i++)
        header[12 + i] = bytes >> (i * 8);
    if (ok)
        ok = !fseek(fp, dev->rtc.size, SEEK_SET) &&
             fwrite(header, sizeof(header), 1, fp) == 1;
    if (fclose(fp))
        ok = 0;
    if (!ok)
        pclog("IBM5140: incomplete retained SRAM append to NVR\n");
}

static void
ibm5140_retention_load(ibm5140_power_t *dev)
{
    uint8_t header[16];
    uint32_t bytes = mem_size * 1024;
    FILE *fp = nvr_fopen(dev->rtc.fn, "rb");
    size_t header_bytes;
    int loaded = 0;

    if (!fp)
        return;
    if (fseek(fp, dev->rtc.size, SEEK_SET)) {
        fclose(fp);
        return;
    }
    header_bytes = fread(header, 1, sizeof(header), fp);
    if (!header_bytes && !ferror(fp)) {
        fclose(fp);
        return; /* An ordinary RTC-only NVR image is a cold start. */
    }
    if (header_bytes == sizeof(header) &&
        !memcmp(header, "IBM5140R\1\0\0\0", 12) &&
        ((uint32_t) header[12] | ((uint32_t) header[13] << 8) |
         ((uint32_t) header[14] << 16) | ((uint32_t) header[15] << 24)) == bytes) {
        loaded = fread(ram, bytes, 1, fp) == 1 && ibm5140_video_load(dev->video, fp) &&
                 fgetc(fp) == EOF && !ferror(fp);
        if (!loaded)
            memset(ram, 0, bytes);
    }
    fclose(fp);
    ibm5140_video_reset(dev->video);
    /* Consume the appended image before POST, preserving the RTC prefix. A
       later hard exit must not resurrect the previous application's save. */
    nvr_save();
    if (loaded)
        dev->power_status &= ~0x30;
    else
        pclog("IBM5140: incompatible/incomplete retained SRAM in NVR discarded\n");
}

static void
ibm5140_suspend(void *priv)
{
    ibm5140_power_t *dev = priv;

    ibm5140_power_nmi(dev, NMI_SUSPEND, 1);
}

static void
ibm5140_cutoff(void *priv)
{
    ibm5140_power_t *dev = priv;

    /* Standby SRAM does not depend on the instantaneous CPU clock state.
     * An unserviced IRQ can hold DISABLE_SLEEP set after BIOS has saved.
     * Retain the physical bytes; POST, not the host, validates the save. */
    dev->qualified = dev->retention_valid;
    dev->powered_off = 1;
    dev->causes = dev->nmi_line = 0;
    nmi = 0;
    speaker_update();
    speaker_mute = 1;
    ibm5140_update_clock(dev);
    /* Use the same host VM shutdown path as ACPI and the soft-power card.
       It saves NVR before memory/device teardown and bypasses exit prompts. */
    plat_power_off();
}

int
ibm5140_power_is_off(void)
{
    return active && active->powered_off;
}

static void
ibm5140_power_button(void *priv)
{
    ibm5140_power_t *dev = priv;

    /* Cutoff is terminal for this process. Do not invalidate its NVR save by
       waking the board while the frontend is still completing shutdown. */
    if (dev->powered_off)
        return;
    softpower_control_write(dev->control, softpower_control_read(dev->control) | 2);
}

void
ibm5140_power_hard_off(void)
{
    if (!active)
        return;
    active->qualified = 0;
    active->power_status = (active->power_status & 0xc0) | 0x20;
    softpower_control_reset(active->control);
    active->causes &= ~NMI_SUSPEND;
    ibm5140_update_nmi(active);
    nvr_dosave = 1;
}

void
ibm5140_power_set_supply(ibm5140_power_t *dev, int external, int low_battery,
                         int rtc_valid, int sram_valid)
{
    if (!dev)
        return;
    dev->power_status = (dev->power_status & 0x30) | (external ? 0x40 : 0) | (low_battery ? 0x80 : 0);
    dev->rtc.regs[RTC_D] = rtc_valid ? 0x80 : 0;
    dev->retention_valid = !!sram_valid;
    if (!sram_valid) {
        dev->qualified = 0;
        dev->power_status |= 0x20;
        if (dev->powered_off)
            memset(ram, 0, mem_size * 1024);
    }
    nvr_dosave = 1;
}

static uint8_t
ibm5140_read(uint16_t port, void *priv)
{
    ibm5140_power_t *dev = priv;
    uint8_t value;

    switch (port) {
        case 0x40:
        case 0x42:
            if (dev->pit_select & 0x40) {
                uint16_t reload = pit_devs[0].get_count(pit_devs[0].data, port & 3);
                return reload >> ((dev->pit_select & 0x80) ? 8 : 0);
            }
            return pit_devs[0].read(port, pit_devs[0].data);
        case 0x41:
            return 0xff; /* No refresh timer channel. */
        case 0x43:
            return dev->pit_control[!!(dev->pit_select & 0x80)];
        case 0x60:
            return dev->compatible;
        case 0x61:
            return dev->port61 | (dev->clear_pending ? 0x80 : 0);
        case 0x62:
            value = ibm5140_sources(dev);
            return value | (pit_devs[0].get_outlevel(pit_devs[0].data, 2) ? 0x20 : 0);
        case 0x63:
            return pic_ibm5140_diag_read(dev->port72);
        case 0x70:
            return dev->rtc_index;
        case 0x71:
            value = dev->rtc.regs[dev->rtc_index];
            if (dev->rtc_index == RTC_A)
                value = (value & 0x7f) | dev->rtc_uip;
            else if (dev->rtc_index == RTC_C) {
                dev->rtc.regs[RTC_C] = 0;
                ibm5140_rtc_irq(dev);
            }
            return value;
        case 0x72:
            return dev->port72;
        case 0x7c:
            return dev->port7c | 0x40;
        case 0x7d:
            value = dev->raw;
            ibm5140_power_nmi(dev, NMI_KEY_DATA, 0);
            return value;
        case 0x7f:
            return dev->power_status | softpower_control_read(dev->control);
        case 0xa0:
            for (unsigned i = 0; i < 8; i++) {
                unsigned irq = (i + pic.priority) & 7;
                if ((pic.isr & (1 << irq)) &&
                    !(pic.special_mask_mode && (pic.imr & (1 << irq))))
                    return (dev->porta0 & 0xf8) | irq;
            }
            return (dev->porta0 & 0xf8) | 7;
        default:
            return 0xff;
    }
}

static void
ibm5140_write(uint16_t port, uint8_t value, void *priv)
{
    ibm5140_power_t *dev = priv;
    uint8_t old;

    switch (port) {
        case 0x40:
        case 0x42:
            pit_devs[0].write(port, value, pit_devs[0].data);
            break;
        case 0x41:
            break;
        case 0x43:
            /* Bit6 selects the Convertible suspend view, not channel 1 or
               8254 readback. Bit7 selects timer mode and initial-count byte:
               50h = timer0/low, D0h = timer2/high. Reads leave counters alone. */
            dev->pit_select = value;
            if (value & 0x40)
                break;
            if (value & 0x30)
                dev->pit_control[!!(value & 0x80)] = value;
            pit_devs[0].write(port, value, pit_devs[0].data);
            break;
        case 0x60:
            dev->compatible = value;
            picintc(2);
            picint(2);
            break;
        case 0x61:
            old = dev->port61 | (dev->clear_pending ? 0x80 : 0);
            speaker_update();
            dev->port61 = value;
            speaker_gated = value & 1;
            speaker_enable = value & 2;
            if (speaker_enable)
                was_speaker_enable = 1;
            speaker_mute = !(value & 4);
            pit_devs[0].set_gate(pit_devs[0].data, 2, value & 1);
            if ((value & 0x80) && !(old & 0x80)) {
                picintc(2);
                dev->clear_pending = 1;
                timer_set_delay_u64(&dev->clear_timer, TIMER_USEC);
            } else if (!(value & 0x80) && !(pic.isr & 2)) {
                dev->clear_pending = 0;
                timer_disable(&dev->clear_timer);
            }
            ibm5140_update_nmi(dev);
            break;
        case 0x63:
            dev->interrupt_simulation = value;
            pic_ibm5140_diag_write(value);
            ibm5140_update_nmi(dev);
            break;
        case 0x70:
            dev->rtc_index = value & 0x3f;
            break;
        case 0x71:
            old = dev->rtc.regs[dev->rtc_index];
            if (dev->rtc_index == RTC_C || dev->rtc_index == RTC_D)
                break;
            if (dev->rtc_index == RTC_A || dev->rtc_index == 0)
                value &= 0x7f;
            if (dev->rtc_index == RTC_B && (value & RTC_SET)) {
                if (!(old & RTC_SET))
                    value &= ~RTC_UF;
                dev->rtc_uip = 0;
            }
            dev->rtc.regs[dev->rtc_index] = value;
            if (dev->rtc_index == RTC_A && old != value)
                ibm5140_rtc_retime(dev, (old ^ value) & 0x70);
            if (dev->rtc_index == RTC_B)
                ibm5140_rtc_irq(dev);
            if (old != value)
                nvr_dosave = 1;
            break;
        case 0x72:
            old = dev->port72;
            dev->port72 = value;
            if ((value & 0x20) && !(old & 0x20))
                nmi_enable = 1; /* Firmware explicitly rearms nested NMI. */
            ibm5140_update_nmi(dev);
            ibm5140_update_clock(dev);
            break;
        case 0x7c:
            old = dev->port7c;
            dev->port7c = value & ~0x40; /* Attached sense is not writable. */
            if ((old ^ value) & 0x20)
                ibm5140_keyboard_reset(dev);
            ibm5140_update_nmi(dev);
            ibm5140_feature_control(dev->port7c);
            break;
        case 0x7d:
            if (!(dev->port7c & 0x20))
                break;
            if (value == 0x80) {
                ibm5140_keyboard_reset(dev);
                ibm5140_update_nmi(dev);
            } else if (ibm5140_raw_defined(value & 0x7f)) {
                dev->diagnostic[value & 0x7f] = !(value & 0x80);
            }
            break;
        case 0x7f:
            /* Upper bits are supply/event inputs. POR and alarm status remain
               latched until the next physical power/reset transition. */
            softpower_control_write(dev->control, value);
            ibm5140_update_nmi(dev);
            break;
        case 0xa0:
            dev->porta0 = value & 0xf8;
            ibm5140_update_nmi(dev);
            break;
        default:
            break;
    }
}

static void
ibm5140_reset(void *priv)
{
    ibm5140_power_t *dev = priv;

    ibm5140_board_reset(dev);
    dev->power_status = (dev->power_status & 0xc0) | 0x20;
    nvr_dosave = 1;
}

static void
ibm5140_handlers(ibm5140_power_t *dev, int install)
{
    io_handler(install, 0x40, 4, ibm5140_read, NULL, NULL, ibm5140_write, NULL, NULL, dev);
    io_handler(install, 0x60, 4, ibm5140_read, NULL, NULL, ibm5140_write, NULL, NULL, dev);
    io_handler(install, 0x70, 3, ibm5140_read, NULL, NULL, ibm5140_write, NULL, NULL, dev);
    io_handler(install, 0x7c, 2, ibm5140_read, NULL, NULL, ibm5140_write, NULL, NULL, dev);
    io_handler(install, 0x7f, 1, ibm5140_read, NULL, NULL, ibm5140_write, NULL, NULL, dev);
    io_handler(install, 0xa0, 1, ibm5140_read, NULL, NULL, ibm5140_write, NULL, NULL, dev);
}

static void
ibm5140_close(void *priv)
{
    ibm5140_power_t *dev = priv;

    ibm5140_handlers(dev, 0);
    keyboard_set_input_handler(NULL, NULL);
    timer_disable(&dev->scanner);
    timer_disable(&dev->clear_timer);
    timer_stop(&dev->rtc_update);
    timer_stop(&dev->rtc_periodic);
    timer_disable(&dev->rtc.onesec_time);
    softpower_control_destroy(dev->control);
    if (active == dev)
        active = NULL;
    /* Do not leave the core holding a query for a board that is gone. */
    cpu_clock_stop_query = NULL;
    cpu_clock_gated      = 0;
    free(dev->rtc.fn);
    free(dev);
}

static const device_t ibm5140_power_device = {
    .name = "IBM PC Convertible power, RTC and keyboard",
    .internal_name = "ibm5140_power",
    .flags = DEVICE_ONBOARD,
    .close = ibm5140_close,
    .reset = ibm5140_reset,
    .power_button = ibm5140_power_button
};

ibm5140_power_t *
ibm5140_power_create(ibm5140_video_t *video)
{
    ibm5140_power_t *dev = calloc(1, sizeof(*dev));

    dev->video = video;
    dev->control = softpower_control_create(2000, ibm5140_suspend, ibm5140_cutoff,
                                             ibm5140_cpu_reset, dev);
    timer_add(&dev->scanner, ibm5140_scan, dev, 0);
    timer_add(&dev->clear_timer, ibm5140_clear_poll, dev, 0);
    timer_add(&dev->rtc_update, ibm5140_rtc_update, dev, 0);
    timer_add(&dev->rtc_periodic, ibm5140_rtc_periodic, dev, 0);
    active = dev;
    /* Let the 80C88 core follow this board's clock-stop state. */
    cpu_clock_stop_query = ibm5140_cpu_clock_stopped;
    cpu_clock_gated      = 0;
    dev->retention_valid = 1;
    dev->power_status = 0x60; /* External power, cold system POR. */
    ibm5140_board_reset(dev);
    dev->rtc.size = 64;
    dev->rtc.irq = -1;
    dev->rtc.data = dev;
    dev->rtc.reset = ibm5140_rtc_defaults;
    dev->rtc.start = ibm5140_rtc_start;
    nvr_init(&dev->rtc);
    /* This chip owns exact UIP/update phases; do not also tick generic XT RTC. */
    timer_disable(&dev->rtc.onesec_time);
    nvr_set_ven_save(ibm5140_retention_save);
    ibm5140_retention_load(dev);
    ibm5140_rtc_retime(dev, 1);
    timer_set_delay_u64(&dev->scanner, 1000ULL * TIMER_USEC);
    keyboard_scan = 1;
    keyboard_set_input_handler(ibm5140_host_key, dev);
    io_removehandler(0x40, 4, pit_devs[0].read, NULL, NULL, pit_devs[0].write, NULL, NULL,
                     pit_devs[0].data);
    ibm5140_handlers(dev, 1);
    device_add_ex(&ibm5140_power_device, dev);
    return dev;
}
