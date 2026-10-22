/* Clean Intel 8255A implementation for the PC-98 motherboard. */
#include <string.h>
#include <86box/pc98_ppi8255.h>

static void
notify_out(pc98_ppi8255_t *p, unsigned port)
{
    if (p->out_cb)
        p->out_cb(p->opaque, port, p->latch[port]);
}

static void
notify_intr(pc98_ppi8255_t *p, unsigned group, int level)
{
    if (p->intr_cb)
        p->intr_cb(p->opaque, group, level);
}

static void
recalc_a(pc98_ppi8255_t *p)
{
    uint8_t level = 0;
    if (p->mode_a == 1) {
        if (p->dir_a)
            level = p->inte_a_in && p->ibf_a && p->stb_a;
        else
            level = p->inte_a_out && p->obf_a && p->ack_a;
    } else if (p->mode_a == 2) {
        level = (p->inte_a_in && p->ibf_a && p->stb_a) || (p->inte_a_out && p->obf_a && p->ack_a);
    }
    if (level != p->intr_a) {
        p->intr_a = level;
        notify_intr(p, 0, level);
    }
}

static void
recalc_b(pc98_ppi8255_t *p)
{
    uint8_t level = 0;
    if (p->mode_b == 1) {
        if (p->dir_b)
            level = p->inte_b && p->ibf_b && p->stb_b;
        else
            level = p->inte_b && p->obf_b && p->ack_b;
    }
    if (level != p->intr_b) {
        p->intr_b = level;
        notify_intr(p, 1, level);
    }
}

static uint8_t
sample_port(pc98_ppi8255_t *p, unsigned port)
{
    uint8_t value = p->input[port];
    if (p->in_cb)
        value = p->in_cb(p->opaque, port);
    p->input[port] = value;
    return value;
}

static uint8_t
port_c_value(pc98_ppi8255_t *p)
{
    uint8_t pins   = sample_port(p, 2);
    uint8_t out    = p->latch[2];
    uint8_t result = 0;

    /* In mode 0 each nibble independently selects pins or output latch. */
    result |= p->dir_c_lower ? (pins & 0x0f) : (out & 0x0f);
    result |= p->dir_c_upper ? (pins & 0xf0) : (out & 0xf0);

    /* Handshake pins replace the corresponding ordinary PC bits.  Intel's
       assignment is fixed by group/mode. */
    if (p->mode_b == 1) {
        result = (uint8_t) ((result & ~0x07u) | (p->intr_b ? 0x01u : 0) | ((p->dir_b ? p->ibf_b : p->obf_b) ? 0x02u : 0) | ((p->dir_b ? p->stb_b : p->ack_b) ? 0x04u : 0));
    }
    if (p->mode_a == 1) {
        if (p->dir_a) {
            result = (uint8_t) ((result & ~0x38u) | (p->intr_a ? 0x08u : 0) | (p->stb_a ? 0x10u : 0) | (p->ibf_a ? 0x20u : 0));
        } else {
            result = (uint8_t) ((result & ~0xc8u) | (p->intr_a ? 0x08u : 0) | (p->ack_a ? 0x40u : 0) | (p->obf_a ? 0x80u : 0));
        }
    } else if (p->mode_a == 2) {
        result = (uint8_t) ((result & ~0xf8u) | (p->intr_a ? 0x08u : 0) | (p->stb_a ? 0x10u : 0) | (p->ibf_a ? 0x20u : 0) | (p->ack_a ? 0x40u : 0) | (p->obf_a ? 0x80u : 0));
    }
    return result;
}

void
pc98_ppi8255_reset(pc98_ppi8255_t *p)
{
    void            *opaque  = p->opaque;
    pc98_ppi_in_cb   in_cb   = p->in_cb;
    pc98_ppi_out_cb  out_cb  = p->out_cb;
    pc98_ppi_intr_cb intr_cb = p->intr_cb;
    memset(p, 0, sizeof(*p));
    p->opaque  = opaque;
    p->in_cb   = in_cb;
    p->out_cb  = out_cb;
    p->intr_cb = intr_cb;
    p->control = 0x9b; /* mode 0, all inputs after RESET */
    p->dir_a = p->dir_b = p->dir_c_upper = p->dir_c_lower = 1;
    p->stb_a = p->ack_a = p->stb_b = p->ack_b = 1;
    p->obf_a = p->obf_b = 1;
}

void
pc98_ppi8255_init(pc98_ppi8255_t *p, void *opaque,
                  pc98_ppi_in_cb in_cb, pc98_ppi_out_cb out_cb,
                  pc98_ppi_intr_cb intr_cb)
{
    memset(p, 0, sizeof(*p));
    p->opaque  = opaque;
    p->in_cb   = in_cb;
    p->out_cb  = out_cb;
    p->intr_cb = intr_cb;
    pc98_ppi8255_reset(p);
}

static void
set_mode(pc98_ppi8255_t *p, uint8_t v)
{
    p->control = v;
    p->mode_a  = (uint8_t) ((v >> 5) & 3u);
    if (p->mode_a == 3)
        p->mode_a = 2;
    p->mode_b      = (uint8_t) ((v >> 2) & 1u);
    p->dir_a       = (uint8_t) ((v >> 4) & 1u);
    p->dir_c_upper = (uint8_t) ((v >> 3) & 1u);
    p->dir_b       = (uint8_t) ((v >> 1) & 1u);
    p->dir_c_lower = (uint8_t) (v & 1u);

    /* A mode-set command clears the output latches and interrupt enables. */
    p->latch[0] = p->latch[1] = p->latch[2] = 0;
    p->inte_a_in = p->inte_a_out = p->inte_b = 0;
    p->ibf_a = p->ibf_b = 0;
    p->obf_a = p->obf_b = 1;
    p->intr_a = p->intr_b = 0;
    notify_out(p, 0);
    notify_out(p, 1);
    notify_out(p, 2);
    notify_intr(p, 0, 0);
    notify_intr(p, 1, 0);
}

static void
bsr_write(pc98_ppi8255_t *p, uint8_t v)
{
    unsigned bit   = (v >> 1) & 7u;
    uint8_t  state = v & 1u;
    uint8_t  mask  = (uint8_t) (1u << bit);
    if (state)
        p->latch[2] |= mask;
    else
        p->latch[2] &= (uint8_t) ~mask;

    /* BSR also controls INTE latches on handshake-designated PC pins. */
    if ((p->mode_a == 1 && p->dir_a && bit == 4) || (p->mode_a == 2 && bit == 4))
        p->inte_a_in = state;
    if ((p->mode_a == 1 && !p->dir_a && bit == 6) || (p->mode_a == 2 && bit == 6))
        p->inte_a_out = state;
    if (p->mode_b == 1 && bit == 2)
        p->inte_b = state;

    recalc_a(p);
    recalc_b(p);
    notify_out(p, 2);
}

uint8_t
pc98_ppi8255_read(pc98_ppi8255_t *p, unsigned reg)
{
    reg &= 3u;
    if (reg == 3)
        return p->control;
    if (reg == 2)
        return port_c_value(p);
    if (reg == 0) {
        if (p->mode_a && p->dir_a) {
            uint8_t v = p->latch[0];
            p->ibf_a  = 0;
            recalc_a(p);
            return v;
        }
        return p->dir_a || p->mode_a == 2 ? sample_port(p, 0) : p->latch[0];
    }
    if (p->mode_b && p->dir_b) {
        uint8_t v = p->latch[1];
        p->ibf_b  = 0;
        recalc_b(p);
        return v;
    }
    return p->dir_b ? sample_port(p, 1) : p->latch[1];
}

void
pc98_ppi8255_write(pc98_ppi8255_t *p, unsigned reg, uint8_t value)
{
    reg &= 3u;
    if (reg == 3) {
        if (value & 0x80)
            set_mode(p, value);
        else
            bsr_write(p, value);
        return;
    }
    if (reg == 2) {
        p->latch[2] = value;
        notify_out(p, 2);
        return;
    }
    p->latch[reg] = value;
    if (reg == 0 && p->mode_a && !p->dir_a) {
        p->obf_a = 0;
        recalc_a(p);
    }
    if (reg == 1 && p->mode_b && !p->dir_b) {
        p->obf_b = 0;
        recalc_b(p);
    }
    notify_out(p, reg);
}

void
pc98_ppi8255_set_input(pc98_ppi8255_t *p, unsigned port, uint8_t value)
{
    if (port < 3)
        p->input[port] = value;
}
void
pc98_ppi8255_strobe_a(pc98_ppi8255_t *p, int level)
{
    uint8_t old = p->stb_a;
    p->stb_a    = !!level;
    if (!old && p->stb_a && p->dir_a) {
        p->latch[0] = sample_port(p, 0);
        p->ibf_a    = 1;
    }
    recalc_a(p);
}
void
pc98_ppi8255_ack_a(pc98_ppi8255_t *p, int level)
{
    uint8_t old = p->ack_a;
    p->ack_a    = !!level;
    if (!old && p->ack_a && !p->dir_a)
        p->obf_a = 1;
    recalc_a(p);
}
void
pc98_ppi8255_strobe_b(pc98_ppi8255_t *p, int level)
{
    uint8_t old = p->stb_b;
    p->stb_b    = !!level;
    if (!old && p->stb_b && p->dir_b) {
        p->latch[1] = sample_port(p, 1);
        p->ibf_b    = 1;
    }
    recalc_b(p);
}
void
pc98_ppi8255_ack_b(pc98_ppi8255_t *p, int level)
{
    uint8_t old = p->ack_b;
    p->ack_b    = !!level;
    if (!old && p->ack_b && !p->dir_b)
        p->obf_b = 1;
    recalc_b(p);
}
