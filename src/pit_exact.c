/*
 * Intel 8253/8254 edge-state core.
 *
 * Portions of the state model are derived from MartyPC's MIT-licensed PIT
 * implementation, Copyright 2022-2026 Daniel Balsom.
 */
#include <86box/pit_exact.h>

#include <assert.h>
#include <string.h>

static uint8_t canonical_mode(uint8_t raw)
{
    raw &= 7u;
    return raw > 5u ? (uint8_t)(raw & 3u) : raw;
}

static uint32_t reload_value(const pitx_channel_t *c)
{
    if (c->bcd)
        return c->count_register ? c->count_register : 0x10000u;
    return c->count_register ? c->count_register : 0x10000u;
}

static uint16_t visible_count(uint32_t value)
{
    return value == 0x10000u ? 0u : (uint16_t)value;
}

static void update_live_latch(pitx_channel_t *c)
{
    if (!c->count_latched)
        c->output_latch = visible_count(c->counting_element);
}

static uint32_t bcd_decrement(uint32_t value)
{
    if (value == 0u || value == 0x10000u)
        return 0x9999u;

    if ((value & 0x000fu) != 0u)
        return (value - 1u) & 0xffffu;
    if ((value & 0x00f0u) != 0u)
        return (value - 0x0007u) & 0xffffu;
    if ((value & 0x0f00u) != 0u)
        return (value - 0x0067u) & 0xffffu;
    return (value - 0x0667u) & 0xffffu;
}

static void decrement(pitx_channel_t *c)
{
    if (c->bcd)
        c->counting_element = bcd_decrement(c->counting_element);
    else if (c->counting_element == 0u)
        c->counting_element = 0xffffu;
    else if (c->counting_element == 0x10000u)
        c->counting_element = 0xffffu;
    else
        c->counting_element = (c->counting_element - 1u) & 0xffffu;
    update_live_latch(c);
}

static void decrement_n(pitx_channel_t *c, unsigned n)
{
    while (n--)
        decrement(c);
}

static void set_output(pitx_channel_t *c, bool output)
{
    c->output = output;
}

static void load_counting_element(pitx_channel_t *c)
{
    uint32_t load = reload_value(c);
    if (c->type == PITX_8254 && c->mode == 3u && (c->count_register & 1u))
        load &= 0xfffeu;
    c->counting_element = load;
    c->null_count = false;
    c->ce_undefined = false;
    if (c->rw_mode == 3u && c->write_phase == 1u)
        c->incomplete_reload = true;
    update_live_latch(c);

    switch (c->mode) {
        case 0:
            set_output(c, false);
            c->state = PITX_COUNTING;
            break;
        case 1:
            set_output(c, false);
            c->state = PITX_COUNTING;
            break;
        case 2:
            set_output(c, true);
            c->state = c->gate ? PITX_COUNTING : PITX_WAIT_GATE;
            break;
        case 3:
            if (c->toggle_on_reload) {
                set_output(c, !c->output);
                c->toggle_on_reload = false;
            } else if (c->state != PITX_RELOAD_NEXT) {
                set_output(c, true);
            }
            c->state = c->gate ? PITX_COUNTING : PITX_WAIT_GATE;
            break;
        case 4:
            set_output(c, true);
            c->state = PITX_COUNTING;
            break;
        case 5:
            set_output(c, true);
            c->state = PITX_COUNTING;
            break;
        default:
            assert(0);
    }
}

static void reset_channel(pitx_channel_t *c, pitx_type_t type)
{
    memset(c, 0, sizeof(*c));
    c->type = type;
    c->rw_mode = 1u;
    c->initial_load = true;
    c->null_count = true;
    c->state = PITX_WAIT_COUNT;
    /* Power-on mode, count and OUT are electrically undefined.  False is a
     * deterministic safe value that avoids a spurious IRQ0 during POST. */
    c->output = false;
}

void pitx_init(pitx_device_t *pit, pitx_type_t type)
{
    pitx_reset(pit, type);
}

void pitx_reset(pitx_device_t *pit, pitx_type_t type)
{
    memset(pit, 0, sizeof(*pit));
    pit->type = type;
    for (unsigned i = 0; i < 3u; ++i)
        reset_channel(&pit->channel[i], type);
}

static void latch_count(pitx_channel_t *c)
{
    /* Intel explicitly specifies that a second latch command is ignored until
     * the first latched count has been fully read. */
    if (c->count_latched)
        return;
    c->output_latch = visible_count(c->counting_element);
    c->count_latched = true;
    c->read_phase = 0u;
}

static void latch_status(pitx_channel_t *c)
{
    if (c->status_latched)
        return;
    c->status_latch = (uint8_t)((c->output ? 0x80u : 0u) |
                                (c->null_count ? 0x40u : 0u) |
                                (c->control & 0x3fu));
    c->status_latched = true;
}

static void program_mode(pitx_channel_t *c, uint8_t control)
{
    c->control = control;
    c->mode_raw = (uint8_t)((control >> 1u) & 7u);
    c->mode = canonical_mode(c->mode_raw);
    c->rw_mode = (uint8_t)((control >> 4u) & 3u);
    c->bcd = (control & 1u) != 0u;
    c->write_phase = 0u;
    c->read_phase = 0u;
    c->count_latched = false;
    c->status_latched = false;
    c->null_count = true;
    c->armed = false;
    c->initial_load = true;
    c->incomplete_reload = false;
    c->ce_undefined = true;
    c->toggle_on_reload = false;
    c->state = PITX_WAIT_COUNT;
    set_output(c, c->mode != 0u);
}

void pitx_control_write(pitx_device_t *pit, uint8_t value)
{
    unsigned select = value >> 6u;
    pit->last_control = value;

    if (select == 3u) {
        if (pit->type != PITX_8254)
            return;
        /* 8254 read-back. D5/D4 are active low; D3..D1 select counters. */
        for (unsigned i = 0; i < 3u; ++i) {
            if ((value & (uint8_t)(2u << i)) == 0u)
                continue;
            if ((value & 0x20u) == 0u)
                latch_count(&pit->channel[i]);
            if ((value & 0x10u) == 0u)
                latch_status(&pit->channel[i]);
        }
        return;
    }

    pitx_channel_t *c = &pit->channel[select];
    if ((value & 0x30u) == 0u) {
        latch_count(c);
        return;
    }
    program_mode(c, value);
}

static void finalize_write(pitx_channel_t *c)
{
    /* Empirically observed NMOS 8253 behavior for illegal divisor 1. */
    if (c->count_register == 1u) {
        if (c->mode == 2u)
            c->count_register = 2u;
        else if (c->mode == 3u)
            c->count_register = 0u;
    }

    c->null_count = true;
    c->armed = true;

    if (c->initial_load) {
        c->initial_load = false;
        if (c->mode == 1u || c->mode == 5u)
            c->state = PITX_WAIT_TRIGGER;
        else
            c->state = PITX_LOAD_NEXT;
        return;
    }

    /* Mode 0 and 4 accept a new count on the next clock. Periodic and
     * hardware-triggered modes retain the current CE until terminal count or
     * the next GATE trigger.  An interrupted LSB/MSB sequence is the measured
     * exception and forces a reload. */
    if (c->incomplete_reload || c->mode == 0u || c->mode == 4u)
        c->state = PITX_LOAD_NEXT;
    c->incomplete_reload = false;
}

void pitx_data_write(pitx_device_t *pit, unsigned channel, uint8_t value)
{
    if (channel >= 3u)
        return;
    pitx_channel_t *c = &pit->channel[channel];

    switch (c->rw_mode) {
        case 1u:
            c->count_register = value;
            finalize_write(c);
            break;
        case 2u:
            c->count_register = (uint16_t)value << 8u;
            finalize_write(c);
            break;
        case 3u:
            if (c->write_phase == 0u) {
                c->count_register = (uint16_t)((c->count_register & 0xff00u) | value);
                c->write_phase = 1u;
                /* In mode 0, the first byte immediately stops counting and
                 * drives OUT low even before the MSB arrives. */
                if (c->mode == 0u) {
                    c->state = PITX_WAIT_COUNT;
                    set_output(c, false);
                }
            } else {
                c->count_register = (uint16_t)((c->count_register & 0x00ffu) |
                                               ((uint16_t)value << 8u));
                c->write_phase = 0u;
                finalize_write(c);
            }
            break;
        default:
            break;
    }
}

uint8_t pitx_data_read(pitx_device_t *pit, unsigned channel)
{
    if (channel >= 3u)
        return 0xffu;
    pitx_channel_t *c = &pit->channel[channel];

    if (c->status_latched) {
        c->status_latched = false;
        return c->status_latch;
    }

    uint16_t value = c->count_latched ? c->output_latch : visible_count(c->counting_element);

    /* NMOS 8253/compatible undocumented bus behavior: during an unfinished
     * LSB/MSB write, a live read returns the inverted pending LSB. */
    if (!c->count_latched && c->rw_mode == 3u && c->write_phase == 1u)
        return (uint8_t)~(c->count_register & 0xffu);

    switch (c->rw_mode) {
        case 1u:
            c->count_latched = false;
            return (uint8_t)value;
        case 2u:
            c->count_latched = false;
            return (uint8_t)(value >> 8u);
        case 3u:
            if (c->read_phase == 0u) {
                c->read_phase = 1u;
                return (uint8_t)value;
            }
            c->read_phase = 0u;
            c->count_latched = false;
            return (uint8_t)(value >> 8u);
        default:
            return 0u;
    }
}

void pitx_set_gate(pitx_device_t *pit, unsigned channel, bool gate)
{
    if (channel >= 3u)
        return;
    pitx_channel_t *c = &pit->channel[channel];
    bool old = c->gate;
    c->gate = gate;

    if (!old && gate) {
        switch (c->mode) {
            case 1u:
            case 5u:
                if (c->state != PITX_WAIT_COUNT) {
                    c->armed = true;
                    c->state = PITX_LOAD_NEXT;
                }
                break;
            case 2u:
            case 3u:
                if (c->state != PITX_WAIT_COUNT)
                    c->state = PITX_LOAD_NEXT;
                break;
            default:
                break;
        }
    } else if (old && !gate) {
        switch (c->mode) {
            case 2u:
            case 3u:
                set_output(c, true);
                if (c->state != PITX_WAIT_COUNT)
                    c->state = PITX_WAIT_GATE;
                break;
            default:
                break;
        }
    }
}

static void count_mode3(pitx_channel_t *c)
{
    const bool odd = (c->count_register & 1u) != 0u;

    if (odd && c->type == PITX_8254) {
        /* The 8254 masks an odd divisor to an even CE. On the high half,
         * terminal count defers the toggle/reload by one clock; the low half
         * toggles and reloads immediately. This preserves the (N+1)/2 high,
         * (N-1)/2 low duty cycle while exposing the correct live count. */
        decrement_n(c, 2u);
        if (visible_count(c->counting_element) == 0u) {
            if (c->output) {
                c->toggle_on_reload = true;
                c->state = PITX_RELOAD_NEXT;
            } else {
                set_output(c, true);
                c->counting_element = reload_value(c) & 0xfffeu;
                update_live_latch(c);
            }
        }
        return;
    }

    /* NMOS 8253: odd values enter CE. The first high count decrements by one
     * and the first low count by three, then normal -2 counting resumes. */
    if (odd && (c->counting_element & 1u) != 0u)
        decrement_n(c, c->output ? 1u : 3u);
    else
        decrement_n(c, 2u);

    if (visible_count(c->counting_element) == 0u) {
        set_output(c, !c->output);
        c->counting_element = reload_value(c);
        update_live_latch(c);
    }
}

void pitx_tick_channel(pitx_device_t *pit, unsigned channel)
{
    if (channel >= 3u)
        return;
    pitx_channel_t *c = &pit->channel[channel];
    ++c->clocks;

    switch (c->state) {
        case PITX_LOAD_NEXT:
            load_counting_element(c);
            return;
        case PITX_RELOAD_NEXT:
            load_counting_element(c);
            return;
        case PITX_STROBE_RECOVER:
            set_output(c, true);
            c->state = PITX_COUNTING;
            return;
        case PITX_WAIT_TRIGGER:
            /* Hardware-triggered modes expose an undefined CE before the
             * first trigger. NMOS measurements consistently show 0003h. */
            if (c->ce_undefined && c->armed) {
                c->counting_element = 3u;
                update_live_latch(c);
            }
            return;
        case PITX_WAIT_COUNT:
        case PITX_WAIT_GATE:
        case PITX_DONE:
            return;
        case PITX_COUNTING:
            break;
    }

    switch (c->mode) {
        case 0u:
            if (c->gate) {
                decrement(c);
                if (visible_count(c->counting_element) == 0u)
                    set_output(c, true);
            }
            break;
        case 1u:
            decrement(c);
            if (visible_count(c->counting_element) == 0u) {
                if (c->armed)
                    set_output(c, true);
                c->armed = false;
                /* CE keeps free-running after terminal count; only OUT is
                 * latched high until another GATE trigger. */
            }
            break;
        case 2u:
            if (c->gate) {
                decrement(c);
                if (visible_count(c->counting_element) == 1u) {
                    set_output(c, false);
                    c->state = PITX_RELOAD_NEXT;
                }
            }
            break;
        case 3u:
            if (c->gate)
                count_mode3(c);
            break;
        case 4u:
            if (c->gate) {
                decrement(c);
                if (visible_count(c->counting_element) == 0u) {
                    set_output(c, false);
                    c->state = PITX_STROBE_RECOVER;
                }
            }
            break;
        case 5u:
            decrement(c);
            if (visible_count(c->counting_element) == 0u) {
                set_output(c, false);
                c->armed = false;
                c->state = PITX_STROBE_RECOVER;
            }
            break;
        default:
            break;
    }
}

void pitx_tick(pitx_device_t *pit)
{
    for (unsigned i = 0; i < 3u; ++i)
        pitx_tick_channel(pit, i);
}

uint16_t pitx_get_count(const pitx_device_t *pit, unsigned channel)
{
    if (channel >= 3u)
        return 0u;
    return visible_count(pit->channel[channel].counting_element);
}

bool pitx_get_output(const pitx_device_t *pit, unsigned channel)
{
    return channel < 3u && pit->channel[channel].output;
}
