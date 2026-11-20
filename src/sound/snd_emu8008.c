/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Creative Sound Blaster AWE64D (EMU8008) PCI audio controller
 *          implementation, reverse-engineered from EMU8008.VXD (Rev 1.06.000).
 *
 *          PCI IDs:
 *            VEN_1102 & DEV_0003 & SUBSYS_00101102 (CT4600 AWE64D)
 *            VEN_1102 & DEV_0003 & SUBSYS_00301102 (CT4650 AWE64D)
 *            VEN_1102 & DEV_0003 & SUBSYS_00311102 (CT4655 AWE64D)
 *
 *          BAR0 (32 I/O ports):
 *            +0x00..+0x0C : EMU8008 Data0..Data6 (Banks 0..6, 16/32-bit)
 *            +0x0E        : EMU8008 Pointer / Index (8-bit, Bank 7)
 *            +0x10        : Interrupt Status / W1C Ack (32-bit, Bank 12)
 *            +0x14..+0x15 : MPU-401 UART Data (+0x14) / Status+Cmd (+0x15)
 *            +0x16        : AC97 / Codec Access Latch (written 0 before index)
 *            +0x18        : Interrupt Enable / Control (16-bit, Bank 11)
 *            +0x1C        : AC97 / Codec Data (16-bit)
 *            +0x1E        : AC97 / Codec Index (8-bit)
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2026 Jasmine Iwanek.
 */
#ifdef ENABLE_EMU8008_LOG
#include <stdarg.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/gameport.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/snd_ac97.h>
#include <86box/snd_emu8k.h>
#include <86box/snd_mpu401.h>
#include <86box/snd_sb.h>
#include <86box/log.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_EMU8008_LOG
int emu8008_do_log = ENABLE_EMU8008_LOG;

static void
emu8008_log(void *priv, const char *fmt, ...)
{
    if (emu8008_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define emu8008_log(priv, fmt, ...)
#endif

/* PCI Identification */
#define EMU8008_PCI_VENDOR          0x1102
#define EMU8008_PCI_DEVICE          0x0003
#define EMU8008_SUBSYS_CT4600       0x0010
#define EMU8008_SUBSYS_CT4650       0x0030
#define EMU8008_SUBSYS_CT4655       0x0031

/* BAR0 I/O Space Size */
#define EMU8008_IO_SIZE             0x20

/* Interrupt Status bits (IOBase + 0x10, 32-bit W1C) */
#define EMU8008_INT_BIT23           0x00800000u /* Callback +0x3E724 */
#define EMU8008_INT_BIT22_20        0x00700000u /* Callback +0x3E728 */
#define EMU8008_INT_BIT17_16        0x00030000u /* Callback +0x3E72C */
#define EMU8008_INT_BIT9            0x00000200u /* Callback +0x3E73C */
#define EMU8008_INT_MPU_RX          0x00000100u /* Callback +0x3E734 (MPU-401) */
#define EMU8008_INT_BIT7            0x00000080u /* Callback +0x3E738 */
#define EMU8008_INT_WAV_MASK        0x0000005Fu /* Callback +0x3E730 */

/* Interrupt Enable bits (IOBase + 0x18, 16-bit R/W) */
#define EMU8008_INTE_BIT23          0x1000u
#define EMU8008_INTE_BIT22_20       0x0E00u
#define EMU8008_INTE_BIT17_16       0x0080u
#define EMU8008_INTE_BIT9           0x0004u
#define EMU8008_INTE_MPU_RX         0x0002u
#define EMU8008_INTE_BIT7           0x0001u

/* Wave Capture (Bank 0, Reg 0x01) field masks */
#define EMU8008_CAP_PAGE_SHIFT      12
#define EMU8008_CAP_MODE_MASK       0x0300u /* bits [9:8]: 0=stop, 1=mono, 3=stereo */
#define EMU8008_CAP_MODE_SHIFT      8
#define EMU8008_CAP_RATE_MASK       0x00E0u /* bits [7:5]: 0..7 -> 48k..8k Hz */
#define EMU8008_CAP_RATE_SHIFT      5
#define EMU8008_CAP_FRAMES_MASK     0x001Fu /* bits [4:0]: frame count */

static const uint32_t emu8008_cap_rates[8] = {
    48000, 44100, 32000, 24000, 22050, 16000, 11025, 8000
};

typedef struct emu8008_t {
    uint8_t  pci_slot;
    uint8_t  irq_state;
    uint16_t subsys_id;
    uint16_t io_base;
    uint8_t  pci_conf[256];

    /* Core Interrupt & Clock state */
    uint32_t int_status;        /* IOBase + 0x10 */
    uint16_t int_enable;        /* IOBase + 0x18 */
    uint32_t sample_clock;      /* 48 kHz sample counter (Reg 0xE0 bits[19:8]) */
    uint16_t chip_id_reg;       /* Reg 0xE0 low bits (must satisfy & 0x0C == 0x0C) */

    /* EMU8000/EMU8008 indexed register state */
    uint8_t  ptr_reg;           /* IOBase + 0x0E */
    uint32_t bank0_regs[256];   /* Bank 0 (Data0) 32-bit registers */
    uint16_t bank4_regs[256];   /* Bank 4 (Data4) 16-bit registers */
    uint16_t bank5_regs[256];   /* Bank 5 (Data5) 16-bit registers */

    /* Wave Capture / ADC DMA state (Bank 0, Reg 0x01) */
    uint32_t cap_phys_addr;
    uint8_t  cap_mode;          /* 0=stop, 1=mono, 3=stereo */
    uint8_t  cap_rate_idx;      /* 0..7 */
    uint8_t  cap_frames;
    uint32_t cap_buf_pos;
    uint8_t  cap_active;

    /* AC97 / Codec Mixer state (IOBase + 0x16/0x1C/0x1E) */
    uint8_t  ac97_latch;
    uint8_t  ac97_index;
    uint16_t ac97_regs[128];
    uint16_t codec_ext_regs[3]; /* 0xEB..0xED redirected to Bank 0 0x01EB..0x01ED */
    ac97_codec_t *ac97_codec;

    /* Sub-devices */
    emu8k_t  *emu8k;
    mpu_t    *mpu;
    sb_t     *sb_legacy;
    void     *gameport;

    /* 48 kHz sample clock timer */
    pc_timer_t clock_timer;

    void    *log;
} emu8008_t;

static void
emu8008_update_irq(emu8008_t *dev)
{
    int assert_irq = 0;

    if ((dev->int_status & EMU8008_INT_BIT23) && (dev->int_enable & EMU8008_INTE_BIT23))
        assert_irq = 1;
    if ((dev->int_status & EMU8008_INT_BIT22_20) && (dev->int_enable & EMU8008_INTE_BIT22_20))
        assert_irq = 1;
    if ((dev->int_status & EMU8008_INT_BIT17_16) && (dev->int_enable & EMU8008_INTE_BIT17_16))
        assert_irq = 1;
    if ((dev->int_status & EMU8008_INT_MPU_RX) && (dev->int_enable & EMU8008_INTE_MPU_RX))
        assert_irq = 1;
    if ((dev->int_status & EMU8008_INT_BIT9) && (dev->int_enable & EMU8008_INTE_BIT9))
        assert_irq = 1;
    if ((dev->int_status & EMU8008_INT_BIT7) && (dev->int_enable & EMU8008_INTE_BIT7))
        assert_irq = 1;
    if (dev->int_status & EMU8008_INT_WAV_MASK)
        assert_irq = 1;

    if (assert_irq && !dev->irq_state) {
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    } else if (!assert_irq && dev->irq_state) {
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    }
}

static void
emu8008_mpu_irq(void *priv, int state)
{
    emu8008_t *dev = (emu8008_t *) priv;

    if (state)
        dev->int_status |= EMU8008_INT_MPU_RX;
    else
        dev->int_status &= ~EMU8008_INT_MPU_RX;

    emu8008_update_irq(dev);
}

static void
emu8008_clock_tick(void *priv)
{
    emu8008_t *dev = (emu8008_t *) priv;

    dev->sample_clock = (dev->sample_clock + 1) & 0x0FFFu;
    /* ~20.833 us per 48 kHz sample tick */
    timer_advance_u64(&dev->clock_timer, (uint64_t) (20.833333 * TIMER_USEC));
}

/* --- EMU8008 Banked Data Port Helpers --- */

static void
emu8008_put_buffer(int16_t *buffer, int len, void *priv)
{
    emu8008_t *dev = (emu8008_t *) priv;
    if (!dev || !dev->cap_mode || !dev->cap_phys_addr || !buffer || len <= 0)
        return;

    uint32_t rate    = emu8008_cap_rates[dev->cap_rate_idx & 7u];
    uint32_t step_fp = (SOUND_FREQ << 16) / rate;
    uint32_t pos_fp  = 0;
    uint32_t max_fp  = (uint32_t) len << 16;
    int      stereo  = (dev->cap_mode == 3);
    int16_t  out_buf[2048];
    int      out_samples = 0;

    while (pos_fp < max_fp && out_samples < (stereo ? 2046 : 2047)) {
        int idx = (int) (pos_fp >> 16);
        if (stereo) {
            out_buf[out_samples++] = buffer[idx * 2];
            out_buf[out_samples++] = buffer[idx * 2 + 1];
        } else {
            int32_t mono = ((int32_t) buffer[idx * 2] + (int32_t) buffer[idx * 2 + 1]) >> 1;
            out_buf[out_samples++] = (int16_t) mono;
        }
        pos_fp += step_fp;
    }

    uint32_t bytes     = (uint32_t) out_samples * sizeof(int16_t);
    const uint8_t *src = (const uint8_t *) out_buf;
    uint32_t prev_half = dev->cap_buf_pos >> 11;

    while (bytes > 0) {
        uint32_t space = 4096u - dev->cap_buf_pos;
        uint32_t chunk = (bytes < space) ? bytes : space;
        dma_bm_write(dev->cap_phys_addr + dev->cap_buf_pos, src, chunk, 4);
        src += chunk;
        bytes -= chunk;
        dev->cap_buf_pos = (dev->cap_buf_pos + chunk) & 0x0FFFu;
        if ((dev->cap_buf_pos >> 11) != prev_half) {
            prev_half = dev->cap_buf_pos >> 11;
            dev->int_status |= 0x00000001u;
            emu8008_update_irq(dev);
        }
    }
}

static void
emu8008_get_wt_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    emu8008_t *dev = (emu8008_t *) priv;
    if (!dev || !dev->emu8k || !buffer || !len)
        return;
    emu8k_update(dev->emu8k);
    for (uint32_t c = 0; c < (uint32_t) len * 2; c++)
        buffer[c] += dev->emu8k->buffer[c];
    emu8k_reset_buffer(dev->emu8k);
}


static uint32_t
emu8008_read_reg32(emu8008_t *dev, uint8_t bank, uint8_t idx)
{
    if ((bank == 6 || bank == 7) && idx == 0xE0) {
        /* Reg 0xE0: bits[19:8] = sample clock, bits[7:0] = chip ID (0x0C) */
        return ((dev->sample_clock & 0x0FFFu) << 8) | (dev->chip_id_reg & 0xFFu);
    }

    static const uint16_t emu8k_bank_ports[4] = { 0x0620, 0x0A20, 0x0A22, 0x0E20 };

    if (bank == 0 && idx == 0x01 && dev->cap_phys_addr) {
        return (dev->cap_phys_addr & 0xFFFFF000u) |
               ((uint32_t) (dev->cap_mode & 0x3u) << EMU8008_CAP_MODE_SHIFT) |
               ((uint32_t) (dev->cap_rate_idx & 0x7u) << EMU8008_CAP_RATE_SHIFT) |
               (dev->cap_frames & EMU8008_CAP_FRAMES_MASK);
    }

    if (bank == 4)
        return dev->bank4_regs[idx];
    if (bank == 5)
        return dev->bank5_regs[idx];

    if (dev->emu8k && bank < 4)
        return emu8k_inl(emu8k_bank_ports[bank], dev->emu8k);

    if (bank == 0)
        return dev->bank0_regs[idx];

    return 0xFFFFFFFFu;
}

static void
emu8008_write_reg32(emu8008_t *dev, uint8_t bank, uint8_t idx, uint32_t val)
{
    if ((bank == 6 || bank == 7) && idx == 0xE0) {
        dev->chip_id_reg = (uint16_t) ((val & 0xFFu) | 0x0Cu);
        return;
    }

    static const uint16_t emu8k_bank_ports[4] = { 0x0620, 0x0A20, 0x0A22, 0x0E20 };

    if (bank == 0) {
        dev->bank0_regs[idx] = val;
        if (idx == 0x01) {
            dev->cap_phys_addr = val & 0xFFFFF000u;
            dev->cap_mode      = (uint8_t) ((val & EMU8008_CAP_MODE_MASK) >> EMU8008_CAP_MODE_SHIFT);
            dev->cap_rate_idx  = (uint8_t) ((val & EMU8008_CAP_RATE_MASK) >> EMU8008_CAP_RATE_SHIFT);
            dev->cap_frames    = (uint8_t) (val & EMU8008_CAP_FRAMES_MASK);
            if (dev->cap_mode && !dev->cap_active) {
                dev->cap_buf_pos = 0;
                dev->cap_active  = 1;
                sound_in_start_input();
            } else if (!dev->cap_mode && dev->cap_active) {
                dev->cap_active = 0;
                sound_in_stop_input();
            }
            emu8008_log(dev->log, "EMU8008: Wave Capture page=%08X mode=%u rate=%uHz frames=%u\n",
                        dev->cap_phys_addr, dev->cap_mode, emu8008_cap_rates[dev->cap_rate_idx], dev->cap_frames);
        }
        if (dev->emu8k)
            emu8k_outl(0x0620, val, dev->emu8k);
        return;
    }

    if (bank == 4) {
        dev->bank4_regs[idx] = (uint16_t) val;
        return;
    }

    if (bank == 5) {
        dev->bank5_regs[idx] = (uint16_t) val;
        return;
    }

    if (dev->emu8k && bank < 4)
        emu8k_outl(emu8k_bank_ports[bank], val, dev->emu8k);
}

/* --- BAR0 I/O Handlers --- */

static uint8_t
emu8008_inb(uint16_t port, void *priv)
{
    emu8008_t *dev = (emu8008_t *) priv;
    uint16_t off = port - dev->io_base;

    switch (off) {
        case 0x0E:
            return dev->ptr_reg;
        case 0x14:
        case 0x15:
            return dev->mpu ? mpu401_read(port, dev->mpu) : 0xFF;
        case 0x1E:
            return dev->ac97_index;
        default: {
            if (off <= 0x0D) {
                uint8_t bank = (uint8_t) (off >> 1);
                uint32_t v = emu8008_read_reg32(dev, bank, dev->ptr_reg);
                return (uint8_t) ((v >> ((off & 1) * 8)) & 0xFFu);
            }
            return 0xFF;
        }
    }
}

static uint16_t
emu8008_inw(uint16_t port, void *priv)
{
    emu8008_t *dev = (emu8008_t *) priv;
    uint16_t off = port - dev->io_base;

    if (off <= 0x0D) {
        uint8_t bank = (uint8_t) (off >> 1);
        return (uint16_t) (emu8008_read_reg32(dev, bank, dev->ptr_reg) & 0xFFFFu);
    }

    switch (off) {
        case 0x0E:
            return (uint16_t) (emu8008_read_reg32(dev, 7, dev->ptr_reg) & 0xFFFFu);
        case 0x16:
            return 0x0000;
        case 0x18:
            return dev->int_enable;
        case 0x1C:
            if (dev->ac97_index >= 0xEB && dev->ac97_index <= 0xED)
                return dev->codec_ext_regs[dev->ac97_index - 0xEB];
            if (dev->ac97_codec)
                return ac97_codec_readw(dev->ac97_codec, dev->ac97_index);
            return dev->ac97_regs[dev->ac97_index & 0x7Fu];
        default:
            return 0xFFFF;
    }
}

static uint32_t
emu8008_inl(uint16_t port, void *priv)
{
    emu8008_t *dev = (emu8008_t *) priv;
    uint16_t off = port - dev->io_base;

    if (off <= 0x0D) {
        uint8_t bank = (uint8_t) (off >> 1);
        return emu8008_read_reg32(dev, bank, dev->ptr_reg);
    }

    switch (off) {
        case 0x0E:
            return emu8008_read_reg32(dev, 7, dev->ptr_reg);
        case 0x10:
            return dev->int_status;
        default:
            return 0xFFFFFFFFu;
    }
}

static void
emu8008_outb(uint16_t port, uint8_t val, void *priv)
{
    emu8008_t *dev = (emu8008_t *) priv;
    uint16_t off = port - dev->io_base;

    switch (off) {
        case 0x0E:
            dev->ptr_reg = val;
            if (dev->emu8k)
                emu8k_outw(0x0E22, val, dev->emu8k);
            break;
        case 0x14:
        case 0x15:
            if (dev->mpu)
                mpu401_write(port, val, dev->mpu);
            break;
        case 0x16:
            dev->ac97_latch = val;
            break;
        case 0x1E:
            dev->ac97_index = val;
            break;
        default:
            if (off <= 0x0D) {
                uint8_t bank = (uint8_t) (off >> 1);
                uint32_t cur = emu8008_read_reg32(dev, bank, dev->ptr_reg);
                uint32_t shift = (uint32_t) (off & 1) * 8u;
                cur = (cur & ~(0xFFu << shift)) | ((uint32_t) val << shift);
                emu8008_write_reg32(dev, bank, dev->ptr_reg, cur);
            }
            break;
    }
}

static void
emu8008_outw(uint16_t port, uint16_t val, void *priv)
{
    emu8008_t *dev = (emu8008_t *) priv;
    uint16_t off = port - dev->io_base;

    if (off <= 0x0D) {
        uint8_t bank = (uint8_t) (off >> 1);
        emu8008_write_reg32(dev, bank, dev->ptr_reg, val);
        return;
    }

    switch (off) {
        case 0x0E:
            emu8008_write_reg32(dev, 7, dev->ptr_reg, val);
            break;
        case 0x16:
            dev->ac97_latch = (uint8_t) val;
            break;
        case 0x18:
            dev->int_enable = val;
            emu8008_update_irq(dev);
            break;
        case 0x1C:
            if (dev->ac97_index >= 0xEB && dev->ac97_index <= 0xED) {
                dev->codec_ext_regs[dev->ac97_index - 0xEB] = val;
            } else {
                dev->ac97_regs[dev->ac97_index & 0x7Fu] = val;
                if (dev->ac97_codec)
                    ac97_codec_writew(dev->ac97_codec, dev->ac97_index, val);
            }
            break;
        default:
            break;
    }
}

static void
emu8008_outl(uint16_t port, uint32_t val, void *priv)
{
    emu8008_t *dev = (emu8008_t *) priv;
    uint16_t off = port - dev->io_base;

    if (off <= 0x0D) {
        uint8_t bank = (uint8_t) (off >> 1);
        emu8008_write_reg32(dev, bank, dev->ptr_reg, val);
        return;
    }

    switch (off) {
        case 0x10:
            /* W1C interrupt acknowledge */
            dev->int_status &= ~val;
            emu8008_update_irq(dev);
            break;
        default:
            break;
    }
}

/* --- PCI Configuration Space --- */

static void
emu8008_io_remap(emu8008_t *dev, uint16_t new_base)
{
    if (dev->io_base) {
        io_removehandler(dev->io_base, EMU8008_IO_SIZE,
                         emu8008_inb, emu8008_inw, emu8008_inl,
                         emu8008_outb, emu8008_outw, emu8008_outl, dev);
    }

    dev->io_base = new_base & ~(EMU8008_IO_SIZE - 1);

    if (dev->io_base && (dev->pci_conf[0x04] & PCI_COMMAND_IO)) {
        io_sethandler(dev->io_base, EMU8008_IO_SIZE,
                      emu8008_inb, emu8008_inw, emu8008_inl,
                      emu8008_outb, emu8008_outw, emu8008_outl, dev);
        if (dev->mpu)
            mpu401_change_addr(dev->mpu, dev->io_base + 0x14);
        emu8008_log(dev->log, "EMU8008: BAR0 mapped at %04X\n", dev->io_base);
    }
}

static uint8_t
emu8008_pci_read(int func, int addr, UNUSED(int len), void *priv)
{
    const emu8008_t *dev = (const emu8008_t *) priv;

    if (func != 0)
        return 0xFF;

    return dev->pci_conf[addr];
}

static void
emu8008_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    emu8008_t *dev = (emu8008_t *) priv;

    if (func != 0)
        return;

    switch (addr) {
        case 0x04:
            dev->pci_conf[0x04] = val & (PCI_COMMAND_IO | PCI_COMMAND_L_BM);
            emu8008_io_remap(dev, dev->io_base);
            break;
        case 0x10:
            dev->pci_conf[0x10] = (val & 0xE0u) | 0x01u;
            emu8008_io_remap(dev, (uint16_t) dev->pci_conf[0x10] |
                                  ((uint16_t) dev->pci_conf[0x11] << 8));
            break;
        case 0x11:
            dev->pci_conf[0x11] = val;
            emu8008_io_remap(dev, (uint16_t) dev->pci_conf[0x10] |
                                  ((uint16_t) dev->pci_conf[0x11] << 8));
            break;
        case 0x3C:
            dev->pci_conf[0x3C] = val;
            break;
        default:
            break;
    }
}

/* --- Device Lifecycle --- */

static void
emu8008_reset(void *priv)
{
    emu8008_t *dev = (emu8008_t *) priv;

    dev->int_status  = 0;
    dev->int_enable  = 0;
    dev->ptr_reg     = 0;
    dev->chip_id_reg = 0x0C;
    dev->cap_mode    = 0;
    memset(dev->bank0_regs, 0, sizeof(dev->bank0_regs));
    memset(dev->bank4_regs, 0, sizeof(dev->bank4_regs));
    memset(dev->bank5_regs, 0, sizeof(dev->bank5_regs));
    emu8008_update_irq(dev);
}

static void *
emu8008_init(const device_t *info)
{
    emu8008_t *dev = calloc(1, sizeof(emu8008_t));
    if (!dev)
        return NULL;

    dev->subsys_id = (uint16_t) info->local;

    dev->log = log_open("EMU8008");
    emu8008_log(dev->log, "EMU8008: init subsys=%04X\n", dev->subsys_id);

    /* PCI Configuration Header */
    dev->pci_conf[0x00] = EMU8008_PCI_VENDOR & 0xFF;
    dev->pci_conf[0x01] = EMU8008_PCI_VENDOR >> 8;
    dev->pci_conf[0x02] = EMU8008_PCI_DEVICE & 0xFF;
    dev->pci_conf[0x03] = EMU8008_PCI_DEVICE >> 8;
    dev->pci_conf[0x04] = PCI_COMMAND_IO | PCI_COMMAND_L_BM;
    dev->pci_conf[0x06] = 0x80; /* Fast back-to-back capable */
    dev->pci_conf[0x09] = 0x00; /* Prog IF */
    dev->pci_conf[0x0A] = 0x01; /* Multimedia Audio Controller */
    dev->pci_conf[0x0B] = 0x04;
    dev->pci_conf[0x10] = 0x01; /* BAR0: I/O space (32 bytes) */
    dev->pci_conf[0x2C] = EMU8008_PCI_VENDOR & 0xFF;
    dev->pci_conf[0x2D] = EMU8008_PCI_VENDOR >> 8;
    dev->pci_conf[0x2E] = dev->subsys_id & 0xFF;
    dev->pci_conf[0x2F] = dev->subsys_id >> 8;
    dev->pci_conf[0x3D] = PCI_INTA;

    pci_add_card(PCI_ADD_NORMAL, emu8008_pci_read, emu8008_pci_write, dev, &dev->pci_slot);

    /* Attach AC97 codec, MPU-401 UART, and legacy SB16 emulation */
    dev->ac97_codec = device_add(ac97_codec_get(AC97_CODEC_STAC9708));
    dev->mpu = (mpu_t *) calloc(1, sizeof(mpu_t));
    if (dev->mpu)
        mpu401_init(dev->mpu, 0, 0, M_UART, 1);

    if (dev->mpu)
        mpu401_irq_attach(dev->mpu, emu8008_mpu_irq, NULL, dev);

    /* EMU8000 wavetable core (ROM-less PCI mode) */
    dev->emu8k = (emu8k_t *) calloc(1, sizeof(emu8k_t));
    if (dev->emu8k)
        emu8k_init(dev->emu8k, 0, device_get_config_int("onboard_ram"));

    if (dev->emu8k)
        wavetable_add_handler(emu8008_get_wt_buffer, dev);
    sound_in_add_handler(emu8008_put_buffer, dev);

    /* Creative Gameport Joystick (VIRTUAL\*CTL7009) */
    dev->gameport = gameport_add(&gameport_pnp_device);

    /* Legacy SB16 ISA/DDMA companion (VIRTUAL\SB16EMULATION) */
    dev->sb_legacy = device_add(&sb_emu8008_legacy_device);

    timer_add(&dev->clock_timer, emu8008_clock_tick, dev, 1);
    emu8008_reset(dev);

    return dev;
}

static void
emu8008_close(void *priv)
{
    emu8008_t *dev = (emu8008_t *) priv;
    if (!dev)
        return;

    if (dev->cap_active) {
        dev->cap_active = 0;
        sound_in_stop_input();
    }
    sound_in_remove_handler(emu8008_put_buffer, dev);
    emu8008_io_remap(dev, 0);
    if (dev->log != NULL) {
        log_close(dev->log);
        dev->log = NULL;
    }
    if (dev->emu8k) {
        emu8k_close(dev->emu8k);
        free(dev->emu8k);
    }
    free(dev->mpu);
    free(dev);
}

static const device_config_t emu8008_config[] = {
    {
        .name        = "onboard_ram",
        .description = "Onboard RAM",
        .type        = CONFIG_SELECTION,
        .default_int = 4096,
        .selection   = {
            { .description = "None",  .value = 0     },
            { .description = "512 KB", .value = 512  },
            { .description = "2 MB",  .value = 2048  },
            { .description = "4 MB",  .value = 4096  },
            { .description = "8 MB",  .value = 8192  },
            { .description = ""                      }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t emu8008_ct4600_device = {
    .name          = "Sound Blaster AWE64D (CT4600)",
    .internal_name = "emu8008_ct4600",
    .flags         = DEVICE_PCI | DEVICE_AUDIO_IN,
    .local         = EMU8008_SUBSYS_CT4600,
    .init          = emu8008_init,
    .close         = emu8008_close,
    .reset         = emu8008_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = emu8008_config
};

const device_t emu8008_ct4650_device = {
    .name          = "Sound Blaster AWE64D (CT4650)",
    .internal_name = "emu8008_ct4650",
    .flags         = DEVICE_PCI | DEVICE_AUDIO_IN,
    .local         = EMU8008_SUBSYS_CT4650,
    .init          = emu8008_init,
    .close         = emu8008_close,
    .reset         = emu8008_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = emu8008_config
};

const device_t emu8008_ct4655_device = {
    .name          = "Sound Blaster AWE64D (CT4655)",
    .internal_name = "emu8008_ct4655",
    .flags         = DEVICE_PCI | DEVICE_AUDIO_IN,
    .local         = EMU8008_SUBSYS_CT4655,
    .init          = emu8008_init,
    .close         = emu8008_close,
    .reset         = emu8008_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = emu8008_config
};
