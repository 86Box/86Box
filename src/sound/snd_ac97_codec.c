/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           AC'97 audio codec emulation.
 *
 *
 *
 * Authors:  RichardG, <richardg867@gmail.com>
 *
 *           Copyright 2021 RichardG.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/snd_ac97.h>

static const struct {
    const uint32_t vendor_id, min_rate, max_rate, misc_flags; /* definitions for misc_flags in snd_ac97.h */
    const uint16_t reset_flags, extid_flags,                  /* definitions in snd_ac97.h */
        powerdown_mask;                                       /* bits [7:0] => register 26 bits [15:8]; bits [11:8] => register 2A bits [14:11] */
    const ac97_vendor_reg_t *vendor_regs;                     /* bits [11:8] of index are the page number if applicable (registers [60:6F]) */
    const device_t          *device;
} ac97_codecs[] = {
  // clang-format off
    [AC97_CODEC_AD1881] = {
    .vendor_id = AC97_VENDOR_ID('A', 'D', 'S', 0x40),
    .min_rate = 7000,
    .max_rate = 48000,
    .misc_flags = AC97_MASTER_6B | AC97_MONOOUT | AC97_PCBEEP | AC97_PHONE | AC97_VIDEO | AC97_AUXIN | AC97_POP | AC97_MS | AC97_LPBK,
    .reset_flags = (1 << AC97_3D_SHIFT), /* datasheet contradicts itself on AC97_HPOUT */
    .extid_flags = AC97_VRA,
    .powerdown_mask = 0x0bf,
    .vendor_regs = (const ac97_vendor_reg_t[]) {{0x74, 0x0000, 0xff07}, {0x76, 0x0404, 0xdde5}, {0x78, 48000, 0x0000}, {0x7a, 48000, 0x0000}, {0}},
    .device = &ad1881_device
    },
    [AC97_CODEC_AK4540] = {
    .vendor_id = AC97_VENDOR_ID('A', 'K', 'M', 0x00),
    .misc_flags = AC97_MONOOUT | AC97_PCBEEP | AC97_PHONE | AC97_VIDEO | AC97_AUXIN | AC97_MS | AC97_LPBK,
    .powerdown_mask = 0x01f,
    .device = &ak4540_device
    },
    [AC97_CODEC_ALC100] = {
    .vendor_id = AC97_VENDOR_ID('A', 'L', 'C', 0x20),
    .misc_flags = AC97_AUXOUT | AC97_MONOOUT | AC97_PCBEEP | AC97_PHONE | AC97_VIDEO | AC97_AUXIN | AC97_POP | AC97_MS | AC97_LPBK,
    .reset_flags = (22 << AC97_3D_SHIFT),
    .extid_flags = AC97_AMAP,
    .powerdown_mask = 0x0bf,
    .device = &alc100_device
    },
    [AC97_CODEC_CS4297] = {
    .vendor_id = AC97_VENDOR_ID('C', 'R', 'Y', 0x03),
    .misc_flags = AC97_MASTER_6B | AC97_AUXOUT | AC97_AUXOUT_6B | AC97_MONOOUT | AC97_MONOOUT_6B | AC97_PCBEEP | AC97_PHONE | AC97_VIDEO | AC97_AUXIN | AC97_MS | AC97_LPBK,
    .reset_flags = AC97_HPOUT | AC97_DAC_18B | AC97_ADC_18B,
    .extid_flags = 0,
    .powerdown_mask = 0x07f,
    .vendor_regs = (const ac97_vendor_reg_t[]) {{0x5a, 0x0301, 0x0000}, {0}},
    .device = &cs4297_device
    },
    [AC97_CODEC_CS4297A] = {
    .vendor_id = AC97_VENDOR_ID('C', 'R', 'Y', 0x11),
    .misc_flags = AC97_MASTER_6B | AC97_AUXOUT | AC97_MONOOUT | AC97_PCBEEP | AC97_PHONE | AC97_VIDEO | AC97_AUXIN | AC97_MS | AC97_LPBK,
    .reset_flags = AC97_HPOUT | AC97_DAC_20B | AC97_ADC_18B | (6 << AC97_3D_SHIFT),
    .extid_flags = AC97_AMAP,
    .powerdown_mask = 0x0ff,
    .vendor_regs = (const ac97_vendor_reg_t[]) {{0x5e, 0x0000, 0x01b0}, {0x60, 0x0023, 0x0001}, {0x68, 0x0000, 0xdfff}, {0}},
    .device = &cs4297a_device
    },
    [AC97_CODEC_STAC9708] = {
    .vendor_id = AC97_VENDOR_ID(0x83, 0x84, 0x76, 0x08),
    .misc_flags = AC97_AUXOUT | AC97_MONOOUT | AC97_PCBEEP | AC97_PHONE | AC97_VIDEO | AC97_AUXIN | AC97_MS | AC97_LPBK,
    .reset_flags = (26 << AC97_3D_SHIFT) | AC97_DAC_18B | AC97_ADC_18B,
    .extid_flags = AC97_SDAC,
    .powerdown_mask = 0x2ff,
    .vendor_regs = (const ac97_vendor_reg_t []) {{0x6c, 0x0000, 0x0003}, {0x74, 0x0000, 0x0003}, {0}},
    .device = &stac9708_device
    },
    [AC97_CODEC_STAC9721] = {
    .vendor_id = AC97_VENDOR_ID(0x83, 0x84, 0x76, 0x09),
    .misc_flags = AC97_AUXOUT | AC97_MONOOUT | AC97_PCBEEP | AC97_PHONE | AC97_VIDEO | AC97_AUXIN | AC97_MS | AC97_LPBK,
    .reset_flags = (26 << AC97_3D_SHIFT) | AC97_DAC_18B | AC97_ADC_18B,
    .extid_flags = AC97_AMAP,
    .powerdown_mask = 0x0ff,
    .vendor_regs = (const ac97_vendor_reg_t []) {{0x6c, 0x0000, 0x0000}, {0x6e, 0x0000, 0x0003}, {0x70, 0x0000, 0xffff}, {0x72, 0x0000, 0x0006}, {0x74, 0x0000, 0x0003}, {0x76, 0x0000, 0xffff}, {0x78, 0x0000, 0x3802}, {0}},
    .device = &stac9721_device
    },
    [AC97_CODEC_WM9701A] = {
    .vendor_id = AC97_VENDOR_ID('W', 'M', 'L', 0x00),
    .misc_flags = AC97_AUXOUT | AC97_MONOOUT | AC97_PCBEEP | AC97_PHONE | AC97_VIDEO | AC97_AUXIN | AC97_MS | AC97_LPBK,
    .reset_flags = AC97_DAC_18B | AC97_ADC_18B,
    .extid_flags = 0,
    .powerdown_mask = 0x03f,
    .device = &wm9701a_device
    }
  // clang-format on
};

#ifdef ENABLE_AC97_CODEC_LOG
int ac97_codec_do_log = ENABLE_AC97_CODEC_LOG;

static void
ac97_codec_log(const char *fmt, ...)
{
    va_list ap;

    if (ac97_codec_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ac97_codec_log(fmt, ...)
#endif

static const int32_t codec_attn[] = {
    // clang-format off
        0,     0,     0,     0,      0,      0,      0,      0,    0,    0,     0,     0,     0,     0,     0,     0,
        1,     1,     1,     1,      2,      2,      2,      3,    4,    5,     6,     8,    10,    12,    16,    20,
       25,    32,    41,    51,     65,     82,    103,    130,  164,  206,   260,   327,   412,   519,   653,   822,
     1036,  1304,  1641,  2067,   2602,   3276,   4125,   5192, 6537, 8230, 10362, 13044, 16422, 20674, 26027, 32767,
    41305, 52068, 65636, 82739, 104299, 131477, 165737, 208925
    // clang-format on
};

ac97_codec_t **ac97_codec = NULL, **ac97_modem_codec = NULL;
int            ac97_codec_count = 0, ac97_modem_codec_count = 0,
    ac97_codec_id = 0, ac97_modem_codec_id = 0;

uint16_t
ac97_codec_readw(ac97_codec_t *dev, uint8_t reg)
{
    /* Redirect a read from extended pages 1+ to the right array. */
    reg &= 0x7e;
    uint16_t ret = dev->regs[0x24 >> 1] & 0x000f;
    if ((ret > 0) && (reg >= 0x60) && (reg < 0x6f))
        ret = (ret <= dev->vendor_reg_page_max) ? dev->vendor_reg_pages[(ret << 3) | ((reg & 0x0e) >> 1)] : 0;
    else
        ret = dev->regs[reg >> 1];

    ac97_codec_log("AC97 Codec %d: readw(%02X) = %04X\n", dev->codec_id, reg, ret);

    return ret;
}

void
ac97_codec_writew(ac97_codec_t *dev, uint8_t reg, uint16_t val)
{
    ac97_codec_log("AC97 Codec %d: writew(%02X, %04X)\n", dev->codec_id, reg, val);

    reg &= 0x7e;
    uint16_t i = 0, prev = dev->regs[reg >> 1];
    int      j;

    switch (reg) {
        case 0x00: /* Reset / ID code */
            ac97_codec_reset(dev);
            return;

        case 0x02: /* Master Volume */
            val &= 0xbf3f;

            /* Convert 1xxxxx to 011111 where unsupported, per specification. */
            if (!(dev->misc_flags & AC97_MASTER_6B)) {
clamp_5b:
                if (val & 0x2000)
                    val = (val & ~0x2000) | 0x1f00;
clamp_5b_r:
                if (val & 0x0020)
                    val = (val & ~0x0020) | 0x001f;
            }
            break;

        case 0x04: /* Aux Out Volume */
            if (!(dev->misc_flags & AC97_AUXOUT))
                return;
            val &= 0xbf3f;

            /* Convert 1xxxxx to 011111 where unsupported, per specification. */
            if (!(dev->misc_flags & AC97_AUXOUT_6B))
                goto clamp_5b;
            break;

        case 0x06: /* Mono Out Volume */
            if (!(dev->misc_flags & AC97_MONOOUT))
                return;
            val &= 0x803f;

            /* Convert 1xxxxx to 011111 where unsupported, per specification. */
            if (!(dev->misc_flags & AC97_MONOOUT_6B))
                goto clamp_5b_r;
            break;

        case 0x08: /* Master Tone Control */
            if (!(dev->reset_flags & AC97_TONECTL))
                return;
            val &= 0x0f0f;
            break;

        case 0x0a: /* PC Beep Volume */
            if (dev->misc_flags & AC97_PCBEEP)
                i |= 0x801e;
            if (dev->misc_flags & AC97_PCBEEP_GEN)
                i |= 0x1fe0;
            val &= i;
            break;

        case 0x0c: /* Phone Volume */
            if (!(dev->misc_flags & AC97_PHONE))
                return;
            val &= 0x801f;
            break;

        case 0x0e: /* Mic Volume */
            val &= 0x805f;
            break;

        case 0x10: /* Line In Volume */
        case 0x12: /* CD Volume */
        case 0x18: /* PCM Out Volume */
line_gain:
            val &= 0x9f1f;
            break;

        case 0x14: /* Video Volume */
            if (!(dev->misc_flags & AC97_VIDEO))
                return;
            goto line_gain;

        case 0x16: /* Aux In Volume */
            if (!(dev->misc_flags & AC97_AUXIN))
                return;
            goto line_gain;

        case 0x1a: /* Record Select Control */
            val &= 0x0707;
            break;

        case 0x1c: /* Record Gain */
            val &= 0x8f0f;
            break;

        case 0x1e: /* Record Gain Mic */
            if (!(dev->reset_flags & AC97_MICPCM))
                return;
            val &= 0x800f;
            break;

        case 0x20: /* General Purpose */
            i = AC97_MIX | (dev->misc_flags & (AC97_POP | AC97_MS | AC97_LPBK));
            if (dev->reset_flags >> AC97_3D_SHIFT)
                i |= AC97_3D;
            if (dev->reset_flags & AC97_SIMSTEREO)
                i |= AC97_ST;
            if (dev->reset_flags & AC97_LOUDNESS)
                i |= AC97_LD;
            if (dev->extid_flags & AC97_DRA)
                i |= AC97_DRSS_MASK;
            val &= i;
            break;

        case 0x22: /* 3D Control */
            switch (dev->reset_flags >> AC97_3D_SHIFT) {
                case 1: /* Analog Devices */
                case 6: /* Crystal */
                    val &= 0x000f;
                    break;

                case 22: /* Avance Logic / Realtek */
                    val &= 0x0003;
                    break;

                case 26: /* SigmaTel */
                    i = 0x0003;
                    if (dev->extid_flags & AC97_SDAC)
                        i |= 0x000c;
                    val &= i;
                    break;

                default:
                    return;
            }
            break;

        case 0x24: /* Audio Interrupt and Paging Mechanism */
            if ((dev->extid_flags & AC97_REV_MASK) < AC97_REV_2_3)
                return;
            val &= 0x000f;
            break;

        case 0x26: /* Powerdown Control/Status */
            i   = dev->powerdown_mask << 8;
            val = (val & i) | (prev & ~i);

            /* Update status bits to reflect powerdowns. */
            val = (val & ~0x000f) | (~(val >> 8) & 0x000f);
            if (val & 0x0800) /* PR3 clears both ANL and REF */
                val &= ~0x0004;
            break;

        case 0x28: /* Extended Audio ID */
            if (dev->misc_flags & AC97_DSA)
                i |= 0x0030;
            val = (val & i) | (prev & ~i);
            break;

        case 0x2a: /* Extended Audio Status/Control */
            i = dev->extid_flags & (AC97_VRA | AC97_DRA | AC97_SPDIF | AC97_VRM);
            if (dev->extid_flags & AC97_SPDIF)
                i |= AC97_SPSA_MASK << AC97_SPSA_SHIFT;
            i |= (dev->powerdown_mask << 3) & 0x7800; /* multichannel powerdowns */
            val = (val & i) | (prev & ~i);

            /* Reset DAC sample rates to 48 KHz (96 KHz with DRA) if VRA is being cleared. */
            if (!(val & AC97_VRA)) {
                for (i = 0x2c; i <= 0x30; i += 2)
                    dev->regs[i >> 1] = 48000;
            }

            /* Reset ADC sample rates to 48 KHz if VRM is being cleared. */
            if (!(val & AC97_VRM)) {
                for (i = 0x32; i <= 0x34; i += 2)
                    dev->regs[i >> 1] = 48000;
            }
            break;

        case 0x2c: /* PCM Front DAC Rate */
        case 0x32: /* PCM L/R ADC Rate */
rate:              /* Writable only if VRA/VRM is set. */
            i = (reg >= 0x32) ? AC97_VRM : AC97_VRA;
            if (!(dev->extid_flags & i))
                return;

            /* Limit to supported sample rate range. */
            if (val < dev->min_rate)
                val = dev->min_rate;
            else if (val > dev->max_rate)
                val = dev->max_rate;
            break;

        case 0x2e: /* PCM Surround DAC Rate */
            if (!(dev->extid_flags & AC97_SDAC))
                return;
            goto rate;

        case 0x30: /* PCM LFE DAC Rate */
            if (!(dev->extid_flags & AC97_LDAC))
                return;
            goto rate;

        case 0x34: /* Mic ADC Rate */
            if (!(dev->reset_flags & AC97_MICPCM))
                return;
            goto rate;

        case 0x36: /* Center/LFE Volume */
            if (dev->extid_flags & AC97_LDAC)
                i |= 0xbf00;
            if (dev->extid_flags & AC97_CDAC)
                i |= 0x00bf;
            val &= i;

            /* Convert 1xxxxx to 011111 where unsupported, per specification. */
            if (!(dev->misc_flags & AC97_LFE_6B) && (val & 0x2000))
                val = (val & ~0x2000) | 0x1f00;
            if (!(dev->misc_flags & AC97_CENTER_6B))
                goto clamp_5b_r;
            break;

        case 0x38: /* Surround Volume */
            if (!(dev->extid_flags & AC97_SDAC))
                return;
            val &= 0xbfbf;

            /* Convert 1xxxxx to 011111 where unsupported, per specification. */
            if (!(dev->misc_flags & AC97_SURR_6B))
                goto clamp_5b;
            break;

        case 0x3a: /* S/PDIF Control */
            if (!(dev->extid_flags & AC97_SPDIF))
                return;
            break;

        case 0x60 ... 0x6e: /* Extended */
            /* Get extended register page. */
            i = dev->regs[0x24 >> 1] & 0x000f;

            /* Redirect a write to page 1+ to the right array, part 1. */
            if (i > 0) {
                /* Don't overflow the pages. */
                if (i > dev->vendor_reg_page_max)
                    return;

                /* Get actual previous value. */
                prev = dev->vendor_reg_pages[(i << 3) | ((reg & 0x0e) >> 1)];
            }

            i <<= 8;
            /* fall-through */

        case 0x5a ... 0x5e: /* Vendor Reserved */
        case 0x70 ... 0x7a:
            /* Stop if no vendor-specific registers are defined. */
            if (!dev->vendor_regs)
                return;

            /* Look for a matching vendor-specific register. */
            i |= reg;
            for (j = 0; dev->vendor_regs[j].index; j++) {
                /* If a match was found, inject written bits. */
                if (dev->vendor_regs[j].index == i) {
                    val = (val & dev->vendor_regs[j].write_mask) | (prev & ~dev->vendor_regs[j].write_mask);
                    break;
                }
            }

            /* No match found. */
            if (!dev->vendor_regs[j].index)
                return;

            /* Redirect a write to page 1+ to the right array, part 2. */
            i >>= 8;
            if (i > 0) {
                dev->vendor_reg_pages[(i << 3) | ((reg & 0x0e) >> 1)] = val;
                return;
            }
            break;

        case 0x7c: /* Vendor ID1 */
        case 0x7e: /* Vendor ID2 */
            return;
    }

    dev->regs[reg >> 1] = val;
}

void
ac97_codec_reset(void *priv)
{
    ac97_codec_t *dev = (ac97_codec_t *) priv;
    uint16_t      i, j;

    ac97_codec_log("AC97 Codec %d: reset()\n", dev->codec_id);

    memset(dev->regs, 0, sizeof(dev->regs));

    /* Set default level and gain values. */
    dev->regs[0x02 >> 1] = AC97_MUTE;
    if (dev->misc_flags & AC97_AUXOUT)
        dev->regs[0x04 >> 1] = AC97_MUTE;
    if (dev->misc_flags & AC97_MONOOUT)
        dev->regs[0x06 >> 1] = AC97_MUTE;
    if (dev->misc_flags & AC97_PHONE)
        dev->regs[0x0c >> 1] = AC97_MUTE | 0x0008;
    dev->regs[0x0e >> 1] = AC97_MUTE | 0x0008;                                               /* mic */
    dev->regs[0x10 >> 1] = dev->regs[0x12 >> 1] = dev->regs[0x18 >> 1] = AC97_MUTE | 0x0808; /* line in, CD, PCM out */
    if (dev->misc_flags & AC97_VIDEO)
        dev->regs[0x14 >> 1] = AC97_MUTE | 0x0808;
    if (dev->misc_flags & AC97_AUXIN)
        dev->regs[0x14 >> 1] = AC97_MUTE | 0x0808;
    dev->regs[0x1c >> 1] = AC97_MUTE; /* record gain */
    if (dev->reset_flags & AC97_MICPCM)
        dev->regs[0x1e >> 1] = AC97_MUTE; /* mic record gain */
    if (dev->misc_flags & AC97_LDAC)
        dev->regs[0x36 >> 1] = AC97_MUTE_L;
    if (dev->misc_flags & AC97_CDAC)
        dev->regs[0x36 >> 1] |= AC97_MUTE_R;
    if (dev->misc_flags & AC97_SDAC)
        dev->regs[0x38 >> 1] = AC97_MUTE_L | AC97_MUTE_R;

    /* Set flags. */
    dev->regs[0x00 >> 1] = dev->reset_flags;
    dev->regs[0x26 >> 1] = 0x000f; /* codec ready */
    dev->regs[0x28 >> 1] = (dev->codec_id << 14) | dev->extid_flags;
    ac97_codec_writew(dev, 0x2a, 0x0000); /* reset variable DAC/ADC sample rates */
    i = dev->extid_flags & (AC97_CDAC | AC97_SDAC | AC97_LDAC);
    dev->regs[0x2a >> 1] |= i | (i << 5); /* any additional DACs are ready but powered down */
    if (dev->extid_flags & AC97_SPDIF)
        dev->regs[0x2a >> 1] |= AC97_SPCV;
    if (dev->reset_flags & AC97_MICPCM)
        dev->regs[0x2a >> 1] |= AC97_MADC | AC97_PRL;

    /* Set vendor ID. */
    dev->regs[0x7c >> 1] = dev->vendor_id >> 16;
    dev->regs[0x7e >> 1] = dev->vendor_id;

    /* Set vendor-specific registers. */
    if (dev->vendor_regs) {
        for (j = 0; dev->vendor_regs[j].index; j++) {
            i = (dev->vendor_regs[j].index >> 8) & 0x000f;
            if (i > 0)
                dev->vendor_reg_pages[(i << 3) | (dev->vendor_regs[j].index >> 1)] = dev->vendor_regs[j].value;
            else
                dev->regs[dev->vendor_regs[j].index >> 1] = dev->vendor_regs[j].value;
        }
    }
}

void
ac97_codec_getattn(void *priv, uint8_t reg, int *l, int *r)
{
    ac97_codec_t *dev = (ac97_codec_t *) priv;
    uint16_t      val = dev->regs[reg >> 1];

    /* Apply full mute and powerdowns. */
    int full_mute = (reg < 0x36);
    if ((full_mute && (val & AC97_MUTE)) ||                     /* full mute */
        (dev->regs[0x26 >> 1] & 0x3e00) ||                      /* DAC powerdown */
        ((reg == 0x38) && (dev->regs[0x2a >> 1] & AC97_PRJ))) { /* surround DAC powerdown */
        *l = 0;
        *r = 0;
    } else { /* per-channel mute */
        /* Determine attenuation value. */
        uint8_t l_val = val >> 8, r_val = val;
        if (reg <= 0x06) { /* 6-bit level */
            *l = codec_attn[0x3f - (l_val & 0x3f)];
            *r = codec_attn[0x3f - (r_val & 0x3f)];
        } else { /* 5-bit gain */
            *l = codec_attn[0x47 - (l_val & 0x1f)];
            *r = codec_attn[0x47 - (r_val & 0x1f)];
        }

        /* Apply per-channel mute and center/LFE powerdowns where applicable. */
        if (!full_mute) {
            if ((val & AC97_MUTE_L) ||                                /* left mute */
                ((reg == 0x36) && (dev->regs[0x2a >> 1] & AC97_PRK))) /* LFE DAC powerdown */
                *l = 0;
            if ((val & AC97_MUTE_R) ||                                /* right mute */
                ((reg == 0x36) && (dev->regs[0x2a >> 1] & AC97_PRI))) /* center DAC powerdown */
                *r = 0;
        }
    }

    ac97_codec_log("AC97 Codec %d: getattn(%02X) = %d %d\n", dev->codec_id, reg, *l, *r);
}

uint32_t
ac97_codec_getrate(void *priv, uint8_t reg)
{
    ac97_codec_t *dev = (ac97_codec_t *) priv;

    /* Get configured sample rate, which is always 48000 if VRA/VRM is not set. */
    uint32_t ret = dev->regs[reg >> 1];

    /* If this is the PCM DAC, double sample rate if DRA is set. */
    if ((reg == 0x2c) && (dev->regs[0x2a >> 1] & AC97_DRA))
        ret <<= 1;

    ac97_codec_log("AC97 Codec %d: getrate(%02X) = %d\n", dev->codec_id, reg, ret);

    return ret;
}

static void *
ac97_codec_init(const device_t *info)
{
    ac97_codec_t *dev = malloc(sizeof(ac97_codec_t));
    memset(dev, 0, sizeof(ac97_codec_t));

    dev->vendor_id      = ac97_codecs[info->local].vendor_id;
    dev->min_rate       = ac97_codecs[info->local].min_rate;
    dev->max_rate       = ac97_codecs[info->local].max_rate;
    dev->reset_flags    = ac97_codecs[info->local].reset_flags;
    dev->extid_flags    = ac97_codecs[info->local].extid_flags;
    dev->misc_flags     = ac97_codecs[info->local].misc_flags;
    dev->powerdown_mask = ac97_codecs[info->local].powerdown_mask;
    dev->vendor_regs    = ac97_codecs[info->local].vendor_regs;
    ac97_codec_log("AC97 Codec %d: init(%c%c%c%02X)\n", ac97_codec_id, (dev->vendor_id >> 24) & 0xff, (dev->vendor_id >> 16) & 0xff, (dev->vendor_id >> 8) & 0xff, dev->vendor_id & 0xff);

    /* Associate this codec to the current controller. */
    if (!ac97_codec || (ac97_codec_count <= 0)) {
        pclog("AC97 Codec %d: No controller to associate codec\n", ac97_codec_id);
        return NULL;
    }
    *ac97_codec = dev;
    if (--ac97_codec_count == 0)
        ac97_codec = NULL;
    else
        ac97_codec += sizeof(ac97_codec_t *);
    dev->codec_id = ac97_codec_id++;

    /* Allocate vendor-specific register pages if required. */
    if (dev->vendor_regs) {
        /* Get the highest vendor-specific register page number. */
        int i, j;
        dev->vendor_reg_page_max = 0;
        for (j = 0; dev->vendor_regs[j].index; j++) {
            i = (dev->vendor_regs[j].index >> 8) & 0x000f;
            if (i > dev->vendor_reg_page_max)
                dev->vendor_reg_page_max = i;
        }

        /* Allocate pages 1+. */
        if (dev->vendor_reg_page_max > 0) {
            ac97_codec_log("AC97 Codec %d: Allocating %d vendor-specific register pages\n", dev->codec_id, dev->vendor_reg_page_max);
            i                     = 16 * dev->vendor_reg_page_max;
            dev->vendor_reg_pages = (uint16_t *) malloc(i);
            memset(dev->vendor_reg_pages, 0, i);
        }
    }

    /* Initialize codec registers. */
    ac97_codec_reset(dev);

    return dev;
}

static void
ac97_codec_close(void *priv)
{
    ac97_codec_t *dev = (ac97_codec_t *) priv;
    if (!dev)
        return;

    ac97_codec_log("AC97 Codec %d: close()\n", dev->codec_id);

    if (dev->vendor_reg_pages)
        free(dev->vendor_reg_pages);
    free(dev);
}

const device_t *
ac97_codec_get(int model)
{
    if ((model >= 0) && (model < (sizeof(ac97_codecs) / sizeof(ac97_codecs[0]))))
        return ac97_codecs[model].device;
    else
        return &cs4297a_device; /* fallback */
}

const device_t ad1881_device = {
    .name = "Analog Devices AD1881",
    .internal_name = "ad1881",
    .flags = DEVICE_AC97,
    .local = AC97_CODEC_AD1881,
    .init = ac97_codec_init,
    .close = ac97_codec_close,
    .reset = ac97_codec_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t ak4540_device = {
    .name = "Asahi Kasei AK4540",
    .internal_name = "ak4540",
    .flags = DEVICE_AC97,
    .local = AC97_CODEC_AK4540,
    .init = ac97_codec_init,
    .close = ac97_codec_close,
    .reset = ac97_codec_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t alc100_device = {
    .name = "Avance Logic ALC100",
    .internal_name = "alc100",
    .flags = DEVICE_AC97,
    .local = AC97_CODEC_ALC100,
    .init = ac97_codec_init,
    .close = ac97_codec_close,
    .reset = ac97_codec_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t cs4297_device = {
    .name = "Crystal CS4297",
    .internal_name = "cs4297",
    .flags = DEVICE_AC97,
    .local = AC97_CODEC_CS4297,
    .init = ac97_codec_init,
    .close = ac97_codec_close,
    .reset = ac97_codec_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t cs4297a_device = {
    .name = "Crystal CS4297A",
    .internal_name = "cs4297a",
    .flags = DEVICE_AC97,
    .local = AC97_CODEC_CS4297A,
    .init = ac97_codec_init,
    .close = ac97_codec_close,
    .reset = ac97_codec_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t stac9708_device = {
    .name = "SigmaTel STAC9708",
    .internal_name = "stac9708",
    .flags = DEVICE_AC97,
    .local = AC97_CODEC_STAC9708,
    .init = ac97_codec_init,
    .close = ac97_codec_close,
    .reset = ac97_codec_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t stac9721_device = {
    .name = "SigmaTel STAC9721",
    .internal_name = "stac9721",
    .flags = DEVICE_AC97,
    .local = AC97_CODEC_STAC9721,
    .init = ac97_codec_init,
    .close = ac97_codec_close,
    .reset = ac97_codec_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

const device_t wm9701a_device = {
    .name = "Wolfson WM9701A",
    .internal_name = "wm9701a",
    .flags = DEVICE_AC97,
    .local = AC97_CODEC_WM9701A,
    .init = ac97_codec_init,
    .close = ac97_codec_close,
    .reset = ac97_codec_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
