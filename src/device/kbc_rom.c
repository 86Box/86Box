/* ROM-backed PC87306/8042 keyboard controller with timed PS/2 pin bridging. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/keyboard.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/upi42.h>
#include "cpu.h"

#define KBC_ROM_PATH "roms/kbc/trigem_ami_green_z.bin"
#define KBC_ROM_SIZE 2048

typedef enum {
    PS2_IDLE,
    PS2_HOST_TO_DEVICE,
    PS2_DEVICE_TO_HOST
} ps2_direction_t;

typedef struct {
    ps2_direction_t direction;
    uint8_t         byte;
    uint8_t         bits[12];
    uint8_t         bit_index;
    uint8_t         phase;
    uint8_t         clock;
    uint8_t         data;
    uint8_t         gap;
} kbc_rom_line_t;

typedef struct {
    void          *core;
    uint8_t        rom[KBC_ROM_SIZE];
    uint8_t        old_p2;
    uint8_t        irq_state;
    uint8_t        enabled[2];
    uint8_t        krr;
    uint8_t        ps2_period;
    uint64_t       cycle_delay;
    uint32_t       core_cycles;
    pc_timer_t     cycle_timer;
    kbc_at_port_t *ports[2];
    kbc_rom_line_t lines[2];
} kbc_rom_t;

static uint8_t
kbc_rom_frame_bit(uint8_t byte, int bit)
{
    if (bit == 0)
        return 0;
    if (bit <= 8)
        return (byte >> (bit - 1)) & 1;
    if (bit == 9) {
        uint8_t parity = 1;
        for (int i = 0; i < 8; i++)
            parity ^= (byte >> i) & 1;
        return parity;
    }
    return 1;
}

static void
kbc_rom_line_start_receive(kbc_rom_line_t *line, kbc_at_port_t *port)
{
    line->direction = PS2_DEVICE_TO_HOST;
    line->byte      = (uint8_t) port->out_new;
    line->bit_index = 0;
    line->phase     = 0;
    line->clock     = 1;
    line->data      = 0;
    port->out_new   = -1;
}

static void
kbc_rom_line_step(kbc_rom_t *dev, int channel, uint8_t p2)
{
    kbc_rom_line_t *line           = &dev->lines[channel];
    kbc_at_port_t  *port           = dev->ports[channel];
    const uint8_t   clock_mask     = channel ? 0x08 : 0x40;
    const uint8_t   data_mask      = channel ? 0x04 : 0x80;
    const int       clock_released = !(p2 & clock_mask);
    const int       data_low       = !!(p2 & data_mask);

    if (line->gap) {
        line->gap--;
        return;
    }

    if (line->direction == PS2_IDLE) {
        line->clock = 1;
        line->data  = 1;
        if (data_low && clock_released) {
            line->direction = PS2_HOST_TO_DEVICE;
            line->bit_index = 0;
            line->phase     = 0;
            memset(line->bits, 0, sizeof(line->bits));
        } else if (port && (port->out_new != -1) && clock_released && !data_low) {
            kbc_rom_line_start_receive(line, port);
        }
    }

    if (line->direction == PS2_HOST_TO_DEVICE) {
        /* The device supplies 12 clock pulses: 11 frame bits and an ACK bit. */
        if (line->phase == 0) {
            if (line->bit_index < 11)
                line->bits[line->bit_index] = !data_low;
            line->data  = line->bit_index == 11 ? 0 : 1;
            line->clock = 0;
        } else if (line->phase == dev->ps2_period / 2) {
            line->clock = 1;
        }
        if (++line->phase == dev->ps2_period) {
            line->phase = 0;
            if (++line->bit_index == 12) {
                uint8_t byte   = 0;
                uint8_t parity = 1;
                for (int i = 0; i < 8; i++) {
                    byte |= line->bits[i + 1] << i;
                    parity ^= line->bits[i + 1];
                }
                if (port && (line->bits[0] == 0) && (line->bits[9] == parity) && (line->bits[10] == 1)) {
                    port->dat     = byte;
                    port->wantcmd = 1;
                }
                line->direction = PS2_IDLE;
                line->clock = line->data = 1;
                line->gap                = 64;
            }
        }
    } else if (line->direction == PS2_DEVICE_TO_HOST) {
        if (!clock_released) {
            line->clock = 0;
            return;
        }
        line->data  = kbc_rom_frame_bit(line->byte, line->bit_index);
        line->clock = line->phase >= dev->ps2_period / 4 && line->phase < (3 * dev->ps2_period) / 4 ? 0 : 1;
        if (++line->phase == dev->ps2_period) {
            line->phase = 0;
            if (++line->bit_index == 11) {
                line->direction = PS2_IDLE;
                line->clock = line->data = 1;
                line->gap                = 64;
            }
        }
    }
}

static void
kbc_rom_update_irqs(kbc_rom_t *dev, uint8_t p2)
{
    for (int channel = 0; channel < 2; channel++) {
        const uint8_t bit    = (uint8_t) (1 << channel);
        const int     active = !!(p2 & (0x10 << channel));
        const int     irq    = channel ? 12 : 1;
        if (active && !(dev->irq_state & bit)) {
            picint(1 << irq);
            dev->irq_state |= bit;
        } else if (!active && (dev->irq_state & bit)) {
            picintc(1 << irq);
            dev->irq_state &= ~bit;
        }
    }
}

static void
kbc_rom_cycle(void *priv)
{
    kbc_rom_t *dev = (kbc_rom_t *) priv;
    if ((dev->krr & 0x05) != 0x05) {
        timer_advance_u64(&dev->cycle_timer, dev->cycle_delay);
        return;
    }
    const uint8_t p2 = upi42_port_output(dev->core, 2);
    uint8_t       p1 = 0xff;

    if ((dev->core_cycles++ % 80) == 0) {
        for (int channel = 0; channel < 2; channel++)
            if (dev->ports[channel] && dev->ports[channel]->priv)
                dev->ports[channel]->poll(dev->ports[channel]->priv);
    }

    for (int channel = 0; channel < 2; channel++) {
        kbc_rom_line_step(dev, channel, p2);
        if (!dev->lines[channel].data || (p2 & (channel ? 0x04 : 0x80)))
            p1 &= ~(1 << channel);
    }

    upi42_port_write(dev->core, 1, p1);
    upi42_set_test_inputs(dev->core,
                          dev->lines[0].clock && !(p2 & 0x40),
                          dev->lines[1].clock && !(p2 & 0x08));
    upi42_exec(dev->core);

    const uint8_t new_p2 = upi42_port_output(dev->core, 2);
    if (new_p2 != dev->old_p2) {
        kbc_rom_update_irqs(dev, new_p2);
        if ((new_p2 ^ dev->old_p2) & 0x02) {
            mem_a20_key = !!(new_p2 & 0x02);
            mem_a20_recalc();
            flushmmucache();
        }
        if ((dev->old_p2 & 1) && !(new_p2 & 1))
            softresetx86();
        dev->old_p2 = new_p2;
    }
    timer_advance_u64(&dev->cycle_timer, dev->cycle_delay);
}

void
kbc_rom_set_krr(void *priv, uint8_t value)
{
    kbc_rom_t *dev = (kbc_rom_t *) priv;
    if (!dev)
        return;
    dev->krr = value;
    /* PC87306 X1 runs at 24 MHz; KRR1 selects divide by 3 or by 2. */
    const int mhz    = (value & 0x02) ? 12 : 8;
    dev->cycle_delay = (15ULL * TIMER_USEC) / mhz;
    dev->ps2_period  = mhz == 8 ? 48 : 64;
}

static uint8_t
kbc_rom_read(uint16_t port, void *priv)
{
    kbc_rom_t *dev = (kbc_rom_t *) priv;
    if (port & 4)
        return upi42_sts_read(port, dev->core);
    return upi42_dbb_read(port, dev->core);
}

static void
kbc_rom_write(uint16_t port, uint8_t value, void *priv)
{
    kbc_rom_t *dev = (kbc_rom_t *) priv;
    if (port & 4)
        upi42_cmd_write(port, value, dev->core);
    else
        upi42_dbb_write(port, value, dev->core);
}

void
kbc_rom_handler(int set, uint16_t base, void *priv)
{
    kbc_rom_t *dev = (kbc_rom_t *) priv;
    if (!dev)
        return;
    for (int i = 0; i < 2; i++) {
        const uint16_t port = base + (i ? 4 : 0);
        if (dev->enabled[i])
            io_removehandler(port, 1, kbc_rom_read, NULL, NULL,
                             kbc_rom_write, NULL, NULL, dev);
        dev->enabled[i] = !!set;
        if (set)
            io_sethandler(port, 1, kbc_rom_read, NULL, NULL,
                          kbc_rom_write, NULL, NULL, dev);
    }
}

static void
kbc_rom_reset(void *priv)
{
    kbc_rom_t *dev = (kbc_rom_t *) priv;
    upi42_reset(dev->core);
    dev->core_cycles = 0;
    memset(dev->lines, 0, sizeof(dev->lines));
    for (int i = 0; i < 2; i++) {
        dev->lines[i].clock = 1;
        dev->lines[i].data  = 1;
    }
    dev->old_p2 = upi42_port_output(dev->core, 2);
}

static void *
kbc_rom_init(const device_t *info)
{
    (void) info;
    FILE *fp = rom_fopen(KBC_ROM_PATH, "rb");
    if (!fp)
        return NULL;
    kbc_rom_t *dev = (kbc_rom_t *) calloc(1, sizeof(*dev));
    if (!dev) {
        fclose(fp);
        return NULL;
    }
    if (fread(dev->rom, 1, KBC_ROM_SIZE, fp) != KBC_ROM_SIZE) {
        fclose(fp);
        free(dev);
        return NULL;
    }
    fclose(fp);
    dev->core = upi42_init(UPI42_ROM_8042, dev->rom);
    if (!dev->core) {
        free(dev);
        return NULL;
    }
    kbc_rom_set_krr(dev, 0x0d);
    for (int i = 0; i < 2; i++) {
        kbc_at_ports[i] = (kbc_at_port_t *) calloc(1, sizeof(kbc_at_port_t));
        if (!kbc_at_ports[i]) {
            for (int j = 0; j < i; j++) {
                free(kbc_at_ports[j]);
                kbc_at_ports[j] = NULL;
            }
            upi42_free(dev->core);
            free(dev);
            return NULL;
        }
        kbc_at_ports[i]->out_new = -1;
        dev->ports[i]            = kbc_at_ports[i];
    }
    if (keyboard_type == KEYBOARD_TYPE_INTERNAL)
        device_add_params(&keyboard_at_generic_device, (void *) (uintptr_t) FLAG_PS2_KBD);
    else
        keyboard_add_device();
    kbc_rom_reset(dev);
    kbc_rom_handler(1, 0x0060, dev);
    timer_add(&dev->cycle_timer, kbc_rom_cycle, dev, 1);
    timer_set_delay_u64(&dev->cycle_timer, dev->cycle_delay);
    return dev;
}

static void
kbc_rom_close(void *priv)
{
    kbc_rom_t *dev = (kbc_rom_t *) priv;
    if (!dev)
        return;
    kbc_rom_handler(0, 0x0060, dev);
    for (int i = 0; i < 2; i++) {
        free(dev->ports[i]);
        kbc_at_ports[i] = NULL;
    }
    upi42_free(dev->core);
    free(dev);
}

static int
kbc_rom_available(void)
{
    return rom_present(KBC_ROM_PATH);
}

const device_t kbc_rom_device = {
    .name          = "ROM-backed PC87306 keyboard controller",
    .internal_name = "kbc_rom_pc87306",
    .flags         = DEVICE_KBC,
    .init          = kbc_rom_init,
    .close         = kbc_rom_close,
    .reset         = kbc_rom_reset,
    .available     = kbc_rom_available
};
