/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           Crystal CS423x (SBPro/WSS compatible sound chips) emulation.
 *
 *
 *
 * Authors:  RichardG, <richardg867@gmail.com>
 *
 *           Copyright 2021-2025 RichardG.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/gameport.h>
#include <86box/i2c.h>
#include <86box/io.h>
#include <86box/isapnp.h>
#include <86box/midi.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/rom.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/snd_ad1848.h>
#include <86box/snd_opl.h>
#include <86box/snd_sb.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>

#define PNP_ROM_CS4236B      "roms/sound/crystal/PNPISA01.BIN"

#define CRYSTAL_NOEEPROM 0x100

enum {
    CRYSTAL_CS4232  = 0x32, /* no chip ID; dummy value */
    CRYSTAL_CS4236  = 0x36, /* no chip ID; dummy value */
    CRYSTAL_CS4236B = 0xab, /* report an older revision ID to make the values nice and incremental */
    CRYSTAL_CS4237B = 0xc8,
    CRYSTAL_CS4238B = 0xc9,
    CRYSTAL_CS4235  = 0xdd,
    CRYSTAL_CS4239  = 0xde
};
enum {
    CRYSTAL_RAM_CMD     = 0,
    CRYSTAL_RAM_ADDR_LO = 1,
    CRYSTAL_RAM_ADDR_HI = 2,
    CRYSTAL_RAM_DATA    = 3
};
enum {
    CRYSTAL_SLAM_NONE  = 0,
    CRYSTAL_SLAM_INDEX = 1,
    CRYSTAL_SLAM_BYTE1 = 2,
    CRYSTAL_SLAM_BYTE2 = 3
};

#ifdef ENABLE_CS423X_LOG
int cs423x_do_log = ENABLE_CS423X_LOG;

static void
cs423x_log(const char *fmt, ...)
{
    va_list ap;

    if (cs423x_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define cs423x_log(fmt, ...)
#endif

static const uint8_t slam_init_key[32] = { 0x96, 0x35, 0x9A, 0xCD, 0xE6, 0xF3, 0x79, 0xBC,
                                           0x5E, 0xAF, 0x57, 0x2B, 0x15, 0x8A, 0xC5, 0xE2,
                                           0xF1, 0xF8, 0x7C, 0x3E, 0x9F, 0x4F, 0x27, 0x13,
                                           0x09, 0x84, 0x42, 0xA1, 0xD0, 0x68, 0x34, 0x1A };
static const uint8_t cs4236_default[] = {
    // clang-format off
    /* Chip configuration */
    0x00, 0x03, /* CD-ROM and modem decode */
    0x80, /* misc. config */
    0x80, /* global config */
    0x0b, /* [code base byte (CS4236B+)] / reserved (CS4236) */
    0x20, 0x04, 0x08, 0x10, 0x80, 0x00, 0x00, /* reserved */
    0x00, /* external decode length */
    0x48, /* reserved */
    0x75, 0xb9, 0xfc, /* IRQ routing */
    0x10, 0x03, /* DMA routing */

    /* Default PnP data */
    0x0e, 0x63, 0x42, 0x36, 0xff, 0xff, 0xff, 0xff, 0x00 /* hinted by documentation to be just the header */
    // clang-format on
};

typedef struct cs423x_t {
    void    *pnp_card;
    ad1848_t ad1848;
    sb_t    *sb;
    void    *gameport;
    void    *i2c;
    void    *eeprom;

    uint16_t wss_base;
    uint16_t opl_base;
    uint16_t sb_base;
    uint16_t ctrl_base;
    uint16_t ram_addr;
    uint16_t eeprom_size : 11;
    uint16_t pnp_offset;
    uint16_t pnp_size;
    uint8_t  type;
    uint8_t  ad1848_type;
    uint8_t  regs[8];
    uint8_t  indirect_regs[16];
    uint8_t  eeprom_data[2048];
    uint8_t  ram_data[65536];
    uint8_t  ram_dl : 2;
    uint8_t  opl_wss : 1;
    char    *nvr_path;

    uint8_t                 pnp_enable : 1;
    uint8_t                 key_pos : 5;
    uint8_t                 slam_enable : 1;
    uint8_t                 slam_state : 2;
    uint8_t                 slam_ld;
    uint8_t                 slam_reg;
    isapnp_device_config_t *slam_config;
} cs423x_t;

static void cs423x_slam_enable(cs423x_t *dev, uint8_t enable);
static void cs423x_ctxswitch_write(uint16_t addr, UNUSED(uint8_t val), void *priv);
static void cs423x_pnp_enable(cs423x_t *dev, uint8_t update_rom, uint8_t update_hwconfig);
static void cs423x_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv);
static void cs423x_reset(void *priv);

static void
cs423x_nvram(cs423x_t *dev, uint8_t save)
{
    FILE *fp = nvr_fopen(dev->nvr_path, save ? "wb" : "rb");
    if (fp) {
        if (save)
            fwrite(dev->eeprom_data, sizeof(dev->eeprom_data), 1, fp);
        else
            (void) !fread(dev->eeprom_data, sizeof(dev->eeprom_data), 1, fp);
        fclose(fp);
    } else {
        cs423x_log("CS423x: EEPROM data %s failed\n", save ? "save" : "load");
    }
}

static uint8_t
cs423x_read(uint16_t addr, void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;
    uint8_t   reg = addr & 7;
    uint8_t   ret = dev->regs[reg];

    switch (reg) {
        case 1: /* EEPROM Interface */
            ret &= ~0x04;
            if ((dev->regs[1] & 0x04) && i2c_gpio_get_sda(dev->i2c))
                ret |= 0x04;
            break;

        case 3: /* Control Indirect Access Register (CS4236B+) */
            /* Intel VS440FX BIOS tells CS4236 from CS4232 through the upper bits. Setting them is enough. */
            if (dev->type >= CRYSTAL_CS4236)
                ret |= 0xf0;
            break;

        case 4: /* Control Indirect Data Register (CS4236B+) / Control Data Register (CS4236) */
            if (dev->type >= CRYSTAL_CS4236B)
                ret = dev->indirect_regs[dev->regs[3]];
            break;

        case 5: /* Control/RAM Access */
            /* Reading RAM is undocumented, but performed by:
               - Windows drivers (unknown purpose)
               - Intel VS440FX BIOS (PnP ROM checksum recalculation) */
            if (dev->ram_dl == CRYSTAL_RAM_DATA) {
                ret = dev->ram_data[dev->ram_addr];
                cs423x_log("CS423x: RAM read(%04X) = %02X\n", dev->ram_addr, ret);
                dev->ram_addr++;
            }
            break;

        case 7: /* Global Status (CS4236+) */
            if (dev->type < CRYSTAL_CS4236)
                break;

            /* Context switching: take active context and interrupt flag, then clear interrupt flag. */
            ret &= 0xc0;
            dev->regs[7] &= 0x80;

            if (dev->sb->mpu->state.irq_pending) /* MPU interrupt */
                ret |= 0x08;
            if (dev->ad1848.status & 0x01) /* WSS interrupt */
                ret |= 0x10;
            if (dev->sb->dsp.sb_irq8 || dev->sb->dsp.sb_irq16 || dev->sb->dsp.sb_irq401) /* SBPro interrupt */
                ret |= 0x20;

            break;

        default:
            break;
    }

    cs423x_log("CS423x: read(%X) = %02X\n", reg, ret);

    return ret;
}

static void
cs423x_write(uint16_t addr, uint8_t val, void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;
    uint8_t   reg = addr & 7;

    cs423x_log("CS423x: write(%X, %02X)\n", reg, val);

    switch (reg) {
        case 0: /* Joystick and Power Control */
            if (dev->type <= CRYSTAL_CS4232)
                val &= 0xeb;
            if ((dev->type >= CRYSTAL_CS4235) && (addr == 0) && (val & 0x08)) {
                /* CS4235+ through X26 backdoor only (hence the addr check): WSS off (one-way trip?) */
                io_removehandler(dev->wss_base, 4, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &dev->ad1848);
                io_removehandler(dev->wss_base, 4, NULL, NULL, NULL, cs423x_ctxswitch_write, NULL, NULL, dev);
                dev->wss_base = 0;
            }
            break;

        case 1: /* EEPROM Interface */
            if (dev->type <= CRYSTAL_CS4232)
                val &= 0x37;
            if (val & 0x04)
                i2c_gpio_set(dev->i2c, val & 0x01, val & 0x02);
            break;

        case 2: /* Block Power Down (CS4236+) */
            if (dev->type < CRYSTAL_CS4236)
                return;
            break;

        case 3: /* Control Indirect Access Register (CS4236B+) */
            if (dev->type < CRYSTAL_CS4236) /* must be writable on CS4236 for the aforementioned VS440FX BIOS check */
                return;
            val &= 0x0f;
            break;

        case 4: /* Control Indirect Data Register (CS4236B+) / Control Data Register (CS4236) */
            if (dev->type < CRYSTAL_CS4236) {
                return;
            } else if (dev->type == CRYSTAL_CS4236) {
                val &= 0x40;
                break;
            }
            switch (dev->regs[3] & 0x0f) {
                case 0: /* WSS Master Control */
                    if ((dev->type < CRYSTAL_CS4235) && (val & 0x80))
                        ad1848_init(&dev->ad1848, dev->ad1848_type);
                    val = 0x00;
                    break;

                case 1:         /* Version / Chip ID */
                case 7:         /* Reserved */
                case 10 ... 15: /* unspecified */
                    return;

                case 2: /* 3D Space and {Center|Volume} (CS4237B+) */
                    if (dev->type < CRYSTAL_CS4237B)
                        return;
                    break;

                case 3: /* 3D Enable (CS4237B+) */
                    if (dev->type < CRYSTAL_CS4237B)
                        return;
                    val &= 0xe0;
                    break;

                case 4: /* Consumer Serial Port Enable (CS423[78]B, unused on CS4235+) */
                    if (dev->type < CRYSTAL_CS4237B)
                        return;
                    val &= 0xf0;
                    break;

                case 5: /* Lower Channel Status (CS423[78]B, unused on CS4235+) */
                    if (dev->type < CRYSTAL_CS4237B)
                        return;
                    if (dev->type < CRYSTAL_CS4235) /* bit 0 changed from reserved to unused on CS4235 */
                        val &= 0xfe;
                    break;

                case 6: /* Upper Channel Status (CS423[78]B, unused on CS4235+) */
                    if (dev->type < CRYSTAL_CS4237B)
                        return;
                    break;

                case 8: /* CS9236 Wavetable Control */
                    val &= 0x0f;
                    cs423x_pnp_enable(dev, 0, 0);

                    /* Update WTEN state on the WSS codec. */
                    dev->ad1848.wten = !!(val & 0x08);
                    ad1848_updatevolmask(&dev->ad1848);
                    break;

                case 9: /* Power Management (CS4235+) */
                    if (dev->type < CRYSTAL_CS4235)
                        return;
                    if ((dev->indirect_regs[dev->regs[3]] & 0x80) && !(val & 0x80)) {
                        cs423x_reset(dev);
                        return;
                    }
                    val &= 0x83;
                    break;

                default:
                    break;
            }
            dev->indirect_regs[dev->regs[3]] = val;
            break;

        case 5: /* Control/RAM Access */
            switch (dev->ram_dl) {
                case CRYSTAL_RAM_CMD: /* commands */
                    switch (val) {
                        case 0x55: /* Disable PnP Key */
                            dev->pnp_enable = 0;
                            fallthrough;

                        case 0x5a: /* Update Hardware Configuration Data */
                            cs423x_pnp_enable(dev, 0, 1);
                            break;

                        case 0x56: /* Disable Crystal Key */
                            cs423x_slam_enable(dev, 0);
                            break;

                        case 0x57: /* Jump to ROM */
                            break;

                        case 0xaa: /* Download RAM */
                            dev->ram_dl = CRYSTAL_RAM_ADDR_LO;
                            break;

                        default:
                            break;
                    }
                    break;

                case CRYSTAL_RAM_ADDR_LO: /* low address byte */
                    dev->ram_addr = val;
                    dev->ram_dl = CRYSTAL_RAM_ADDR_HI;
                    break;

                case CRYSTAL_RAM_ADDR_HI: /* high address byte */
                    dev->ram_addr |= val << 8;
                    dev->ram_dl = CRYSTAL_RAM_DATA;
                    cs423x_log("CS423x: RAM start(%04X)\n", dev->ram_addr);
                    break;

                case CRYSTAL_RAM_DATA: /* data */
                    cs423x_log("CS423x: RAM write(%04X, %02X)\n", dev->ram_addr, val);
                    dev->ram_data[dev->ram_addr++] = val;
                    break;

                default:
                    break;
            }
            break;

        case 6: /* RAM Access End */
            /* TriGem Delhi-III BIOS writes undocumented value 0x40 instead of 0x00. */
            if ((val == 0x00) || (val == 0x40)) {
                cs423x_log("CS423x: RAM end\n");
                dev->ram_dl = CRYSTAL_RAM_CMD;

                /* Update PnP state and resource data. */
                dev->pnp_size = (dev->type >= CRYSTAL_CS4236) ? 384 : 256; /* we don't know the length */
                cs423x_pnp_enable(dev, 1, 0);
            }
            break;

        case 7: /* Global Status (CS4236+) */
            return;

        default:
            break;
    }

    dev->regs[reg] = val;
}

static void
cs423x_slam_write(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;
    uint8_t   idx;

    if ((dev->slam_state != CRYSTAL_SLAM_NONE) || (val == slam_init_key[dev->key_pos])) /* cut down on ISAPnP-related noise */
        cs423x_log("CS423x: slam_write(%02X)\n", val);

    switch (dev->slam_state) {
        case CRYSTAL_SLAM_NONE:
            /* Not in SLAM: read and compare Crystal key. */
            if (val == slam_init_key[dev->key_pos]) {
                dev->key_pos++;
                /* Was the key successfully written? */
                if (!dev->key_pos) {
                    /* Discard any pending logical device configuration, just to be safe. */
                    if (dev->slam_config) {
                        free(dev->slam_config);
                        dev->slam_config = NULL;
                    }

                    /* Enter SLAM. */
                    cs423x_log("CS423x: SLAM unlocked\n");
                    dev->slam_state = CRYSTAL_SLAM_INDEX;
                }
            } else {
                dev->key_pos = 0;
            }
            break;

        case CRYSTAL_SLAM_INDEX:
            /* Intercept the Activate Audio Device command. */
            if (val == 0x79) {
                cs423x_log("CS423x: Exiting SLAM\n");

                /* Apply the last logical device's configuration. */
                if (dev->slam_config) {
                    cs423x_pnp_config_changed(dev->slam_ld, dev->slam_config, dev);
                    free(dev->slam_config);
                    dev->slam_config = NULL;
                }

                /* Exit out of SLAM. */
                dev->slam_state = CRYSTAL_SLAM_NONE;
                break;
            }

            /* Write register index. */
            dev->slam_reg   = val;
            dev->slam_state = CRYSTAL_SLAM_BYTE1;
            break;

        case CRYSTAL_SLAM_BYTE1:
        case CRYSTAL_SLAM_BYTE2:
            /* Write register value: two bytes for I/O ports, single byte otherwise. */
            cs423x_log("CS423x: SLAM write(%02X, %02X)\n", dev->slam_reg, val);
            switch (dev->slam_reg) {
                case 0x06: /* Card Select Number */
                    isapnp_set_csn(dev->pnp_card, val);
                    break;

                case 0x15: /* Logical Device ID */
                    /* Apply the previous logical device's configuration, and reuse its config structure. */
                    if (dev->slam_config)
                        cs423x_pnp_config_changed(dev->slam_ld, dev->slam_config, dev);
                    else
                        dev->slam_config = (isapnp_device_config_t *) calloc(1, sizeof(isapnp_device_config_t));

                    /* Start new logical device. */
                    dev->slam_ld = val;
                    break;

                case 0x47: /* I/O Port Base Address 0 */
                case 0x48: /* I/O Port Base Address 1 */
                case 0x42: /* I/O Port Base Address 2 */
                    idx = (dev->slam_reg == 0x42) ? 2 : (dev->slam_reg - 0x47);
                    if (dev->slam_state == CRYSTAL_SLAM_BYTE1) {
                        /* Set high byte, or ignore it if no logical device is selected. */
                        if (dev->slam_config)
                            dev->slam_config->io[idx].base = val << 8;

                        /* Prepare for the second (low byte) write. */
                        dev->slam_state = CRYSTAL_SLAM_BYTE2;
                        return;
                    } else if (dev->slam_config) {
                        /* Set low byte, or ignore it if no logical device is selected. */
                        dev->slam_config->io[idx].base |= val;
                    }
                    break;

                case 0x22: /* Interrupt Select 0 */
                case 0x27: /* Interrupt Select 1 */
                    /* Stop if no logical device is selected. */
                    if (!dev->slam_config)
                        break;

                    /* Set IRQ value. */
                    idx                            = (dev->slam_reg == 0x22) ? 0 : 1;
                    dev->slam_config->irq[idx].irq = val & 15;
                    break;

                case 0x2a: /* DMA Select 0 */
                case 0x25: /* DMA Select 1 */
                    /* Stop if no logical device is selected. */
                    if (!dev->slam_config)
                        break;

                    /* Set DMA value. */
                    idx                            = (dev->slam_reg == 0x2a) ? 0 : 1;
                    dev->slam_config->dma[idx].dma = val & 7;
                    break;

                case 0x33: /* Activate Device */
                    /* Stop if no logical device is selected. */
                    if (!dev->slam_config)
                        break;

                    /* Activate or deactivate the device. */
                    dev->slam_config->activate = val & 0x01;
                    break;

                default:
                    break;
            }

            /* Prepare for the next register, unless a two-byte write returns above. */
            dev->slam_state = CRYSTAL_SLAM_INDEX;
            break;

        default:
            break;
    }
}

static void
cs423x_slam_enable(cs423x_t *dev, uint8_t enable)
{
    /* Disable SLAM. */
    if (dev->slam_enable) {
        dev->slam_state  = CRYSTAL_SLAM_NONE;
        dev->slam_enable = 0;
        io_removehandler(0x279, 1, NULL, NULL, NULL, cs423x_slam_write, NULL, NULL, dev);
    }

    /* Enable SLAM if the CKD bit is not set. */
    if (enable && !(dev->ram_data[0x4002] & 0x10)) {
        cs423x_log("CS423x: Enabling SLAM\n");
        dev->slam_enable = 1;
        io_sethandler(0x279, 1, NULL, NULL, NULL, cs423x_slam_write, NULL, NULL, dev);
    } else {
        cs423x_log("CS423x: Disabling SLAM\n");
    }
}

static void
cs423x_ctxswitch_write(uint16_t addr, UNUSED(uint8_t val), void *priv)
{
    cs423x_t *dev        = (cs423x_t *) priv;
    uint8_t   ctx        = (dev->regs[7] & 0x80);
    uint8_t   enable_opl = (dev->ad1848.xregs[4] & 0x10) && !(dev->indirect_regs[2] & 0x85);

    /* Check if a context switch (WSS=1 <-> SBPro=0) occurred through the address being written. */
    if ((dev->regs[7] & 0x80) ? ((addr & 0xfff0) == dev->sb_base) : ((addr & 0xfffc) == dev->wss_base)) {
        /* Flip context bit. */
        dev->regs[7] ^= 0x80;
        ctx ^= 0x80;
        cs423x_log("CS423x: Context switch to %s\n", ctx ? "WSS" : "SBPro");

        /* Update CD audio filter.
           FIXME: not thread-safe: filter function TOCTTOU in sound_cd_thread! */
        sound_set_cd_audio_filter(NULL, NULL);
        if (ctx) /* WSS */
            sound_set_cd_audio_filter(ad1848_filter_cd_audio, &dev->ad1848);
        else /* SBPro */
            sound_set_cd_audio_filter(sbpro_filter_cd_audio, dev->sb);

        /* Fire a context switch interrupt if enabled. */
        if ((dev->regs[0] & 0x20) && (dev->ad1848.irq > 0)) {
            dev->regs[7] |= 0x40;         /* set interrupt flag */
            picint(1 << dev->ad1848.irq); /* control device shares IRQ with WSS and SBPro */
        }
    }

    /* Update OPL ownership and state regardless of context switch,
       to trap writes to other registers which may disable the OPL. */
    dev->sb->opl_enabled = !ctx && enable_opl;
    dev->opl_wss         = ctx && enable_opl;
}

static void
cs423x_get_buffer(int32_t *buffer, int len, void *priv)
{
    cs423x_t       *dev = (cs423x_t *) priv;

    /* Output audio from the WSS codec, and also the OPL if we're in charge of it. */
    ad1848_update(&dev->ad1848);

    /* Don't output anything if the analog section or DAC is powered down. */
    if (!(dev->regs[2] & 0xb4) && !(dev->indirect_regs[9] & 0x04)) {
        for (int c = 0; c < len * 2; c += 2) {
            buffer[c] += dev->ad1848.buffer[c] / 2;
            buffer[c + 1] += dev->ad1848.buffer[c + 1] / 2;
        }
    }

    dev->ad1848.pos = 0;
}

static void
cs423x_get_music_buffer(int32_t *buffer, int len, void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;

    /* Output audio from the WSS codec, and also the OPL if we're in charge of it. */
    if (dev->opl_wss) {
        const int32_t *opl_buf = dev->sb->opl.update(dev->sb->opl.priv);

        /* Don't output anything if the analog section, DAC (DAC2 instead on CS4235+) or FM synth is powered down. */
        uint8_t bpd_mask = (dev->type >= CRYSTAL_CS4235) ? 0xb1 : 0xb5;
        if (!(dev->regs[2] & bpd_mask) && !(dev->indirect_regs[9] & 0x06)) {
            for (int c = 0; c < len * 2; c += 2) {
                buffer[c] += (opl_buf[c] * dev->ad1848.fm_vol_l) >> 16;
                buffer[c + 1] += (opl_buf[c + 1] * dev->ad1848.fm_vol_r) >> 16;
            }
        }

        dev->sb->opl.reset_buffer(dev->sb->opl.priv);
    }
}

static void
cs423x_pnp_enable(cs423x_t *dev, uint8_t update_rom, uint8_t update_hwconfig)
{
    cs423x_log("CS423x: Updating PnP ROM=%d hwconfig=%d\n", update_rom, update_hwconfig);

    if (dev->pnp_card) {
        /* Update PnP resource data if requested. */
        if (update_rom)
            isapnp_update_card_rom(dev->pnp_card, &dev->ram_data[dev->pnp_offset], dev->pnp_size);

        /* Disable PnP key if the PKD bit is set, or if it was disabled by command 0x55. */
        /* But wait! The TriGem Delhi-III BIOS sends command 0x55, and its behavior doesn't
           line up with real hardware (still listed in the POST summary and seen by software).
           Disable the PnP key disabling mechanism until someone figures something out. */
#if 0
        isapnp_enable_card(dev->pnp_card, ((dev->ram_data[0x4002] & 0x20) || !dev->pnp_enable) ? ISAPNP_CARD_NO_KEY : ISAPNP_CARD_ENABLE);
#else
        if ((dev->ram_data[0x4002] & 0x20) || !dev->pnp_enable)
            pclog("CS423x: Attempted to disable PnP key\n");
#endif
    }

    /* Update some register bits based on the config data in RAM if requested. */
    if (update_hwconfig) {
        /* Update WTEN. */
        if (dev->ram_data[0x4003] & 0x08) {
            dev->indirect_regs[8] |= 0x08;
            dev->ad1848.wten = 1;
        } else {
            dev->indirect_regs[8] &= ~0x08;
            dev->ad1848.wten = 0;
        }

        /* Update SPS. */
        if ((dev->type >= CRYSTAL_CS4236B) && (dev->type <= CRYSTAL_CS4238B)) {
            if (dev->ram_data[0x4003] & 0x04)
                dev->indirect_regs[8] |= 0x04;
            else
                dev->indirect_regs[8] &= ~0x04;
        }

        /* Update IFM. */
        if (dev->ram_data[0x4003] & 0x80)
            dev->ad1848.xregs[4] |= 0x10;
        else
            dev->ad1848.xregs[4] &= ~0x10;

        if (dev->type == CRYSTAL_CS4236) {
            /* Update VCEN. */
            if (dev->ram_data[0x4002] & 0x04)
                dev->regs[4] |= 0x40;
            else
                dev->regs[4] &= ~0x40;            
        }

        if (dev->type >= CRYSTAL_CS4235) {
            /* Update X18 and X19 values. */
            dev->ad1848.xregs[18] = (dev->ad1848.xregs[18] & ~0x3e) | (dev->ram_data[0x400b] & 0x3e);
            dev->ad1848.xregs[19] = dev->ram_data[0x4005];
        }

        /* Inform WSS codec of the changes. */
        ad1848_updatevolmask(&dev->ad1848);
    }
}

static void
cs423x_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;

    switch (ld) {
        case 0: /* WSS, OPL3 and SBPro */
            if (dev->wss_base) {
                io_removehandler(dev->wss_base, 4, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &dev->ad1848);
                io_removehandler(dev->wss_base, 4, NULL, NULL, NULL, cs423x_ctxswitch_write, NULL, NULL, dev);
                dev->wss_base = 0;
            }

            if (dev->opl_base) {
                io_removehandler(dev->opl_base, 4, dev->sb->opl.read, NULL, NULL, dev->sb->opl.write, NULL, NULL, dev->sb->opl.priv);
                dev->opl_base = 0;
            }

            if (dev->sb_base) {
                sb_dsp_setaddr(&dev->sb->dsp, 0);
                io_removehandler(dev->sb_base, 4, dev->sb->opl.read, NULL, NULL, dev->sb->opl.write, NULL, NULL, dev->sb->opl.priv);
                io_removehandler(dev->sb_base + 8, 2, dev->sb->opl.read, NULL, NULL, dev->sb->opl.write, NULL, NULL, dev->sb->opl.priv);
                io_removehandler(dev->sb_base + 4, 2, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, dev->sb);
                io_removehandler(dev->sb_base, 16, NULL, NULL, NULL, cs423x_ctxswitch_write, NULL, NULL, dev);
                dev->sb_base = 0;
            }

            ad1848_setirq(&dev->ad1848, 0);
            sb_dsp_setirq(&dev->sb->dsp, 0);

            ad1848_setdma(&dev->ad1848, 0);
            sb_dsp_setdma8(&dev->sb->dsp, 0);

            if (config->activate) {
                if (config->io[0].base != ISAPNP_IO_DISABLED) {
                    dev->wss_base = config->io[0].base;
                    io_sethandler(dev->wss_base, 4, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &dev->ad1848);
                    io_sethandler(dev->wss_base, 4, NULL, NULL, NULL, cs423x_ctxswitch_write, NULL, NULL, dev);
                }

                if (config->io[1].base != ISAPNP_IO_DISABLED) {
                    dev->opl_base = config->io[1].base;
                    io_sethandler(dev->opl_base, 4, dev->sb->opl.read, NULL, NULL, dev->sb->opl.write, NULL, NULL, dev->sb->opl.priv);
                }

                if (config->io[2].base != ISAPNP_IO_DISABLED) {
                    dev->sb_base = config->io[2].base;
                    sb_dsp_setaddr(&dev->sb->dsp, dev->sb_base);
                    io_sethandler(dev->sb_base, 4, dev->sb->opl.read, NULL, NULL, dev->sb->opl.write, NULL, NULL, dev->sb->opl.priv);
                    io_sethandler(dev->sb_base + 8, 2, dev->sb->opl.read, NULL, NULL, dev->sb->opl.write, NULL, NULL, dev->sb->opl.priv);
                    io_sethandler(dev->sb_base + 4, 2, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, dev->sb);
                    io_sethandler(dev->sb_base, 16, NULL, NULL, NULL, cs423x_ctxswitch_write, NULL, NULL, dev);
                }

                if (config->irq[0].irq != ISAPNP_IRQ_DISABLED) {
                    ad1848_setirq(&dev->ad1848, config->irq[0].irq);
                    sb_dsp_setirq(&dev->sb->dsp, config->irq[0].irq);
                }

                if (config->dma[0].dma != ISAPNP_DMA_DISABLED) {
                    ad1848_setdma(&dev->ad1848, config->dma[0].dma);
                    sb_dsp_setdma8(&dev->sb->dsp, config->dma[0].dma);
                }
            }
            break;

        case 1: /* Game Port */
            if (dev->gameport)
                gameport_remap(dev->gameport, (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) ? config->io[0].base : 0);
            break;

        case 2: /* Control Registers */
            if (dev->ctrl_base) {
                io_removehandler(dev->ctrl_base, 8, cs423x_read, NULL, NULL, cs423x_write, NULL, NULL, dev);
                dev->ctrl_base = 0;
            }

            if (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) {
                dev->ctrl_base = config->io[0].base;
                io_sethandler(dev->ctrl_base, 8, cs423x_read, NULL, NULL, cs423x_write, NULL, NULL, dev);
            }

            break;

        case 3: /* MPU-401 */
            mpu401_change_addr(dev->sb->mpu, 0);
            mpu401_setirq(dev->sb->mpu, 0);

            if (config->activate) {
                if (config->io[0].base != ISAPNP_IO_DISABLED)
                    mpu401_change_addr(dev->sb->mpu, config->io[0].base);

                if (config->irq[0].irq != ISAPNP_IRQ_DISABLED)
                    mpu401_setirq(dev->sb->mpu, config->irq[0].irq);
            }

            break;

        default:
            break;
    }
}

static void
cs423x_load_defaults(cs423x_t *dev, uint8_t *dest)
{
    switch (dev->type) {
        case CRYSTAL_CS4236:
        case CRYSTAL_CS4236B:
        case CRYSTAL_CS4237B:
        case CRYSTAL_CS4238B:
        case CRYSTAL_CS4235:
        case CRYSTAL_CS4239:
            memcpy(dest, cs4236_default, sizeof(cs4236_default));
            dev->pnp_size = 9; /* header-only PnP ROM size */

            switch (dev->type) {
                case CRYSTAL_CS4236:
                    dest[4] = 0x43; /* code base byte */
                    break;

                case CRYSTAL_CS4235:
                case CRYSTAL_CS4239:
                    dest[4]  = 0x05; /* code base byte */
                    dest[12] = 0x08; /* external decode length */
                    break;
            }
            break;
    }
}

static void
cs423x_reset(void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;

    /* Clear RAM. */
    memset(dev->ram_data, 0, sizeof(dev->ram_data));

    /* Load default configuration data to RAM. */
    cs423x_load_defaults(dev, &dev->ram_data[0x4000]);

    if (dev->eeprom) {
        /* Load EEPROM data to RAM if the magic bytes are present. */
        if ((dev->eeprom_data[0] == 0x55) && (dev->eeprom_data[1] == 0xbb)) {
            cs423x_log("CS423x: EEPROM data valid, loading to RAM\n");
            dev->pnp_size = (dev->eeprom_data[2] << 8) | dev->eeprom_data[3];
            if (dev->pnp_size > 384)
                dev->pnp_size = 384;
            memcpy(&dev->ram_data[0x4000], &dev->eeprom_data[4], sizeof(dev->eeprom_data) - 4);
        } else {
            cs423x_log("CS423x: EEPROM data invalid, ignoring\n");
        }

        /* Save EEPROM contents to file. */
        cs423x_nvram(dev, 1);
    }

    /* Reset registers. */
    memset(dev->regs, 0, sizeof(dev->regs));
    dev->regs[1] = 0x80;
    memset(dev->indirect_regs, 0, sizeof(dev->indirect_regs));
    dev->indirect_regs[1] = dev->type;
    if (dev->type == CRYSTAL_CS4238B)
        dev->indirect_regs[2] = 0x20;

    /* Reset WSS codec. */
    ad1848_init(&dev->ad1848, dev->ad1848_type);

    /* Reset PnP resource data, state and logical devices. */
    dev->pnp_enable = 1;
    cs423x_pnp_enable(dev, 1, 1);
    if (dev->pnp_card && dev->sb)
        isapnp_reset_card(dev->pnp_card);

    /* Reset SLAM. */
    cs423x_slam_enable(dev, 1);
}

static void *
cs423x_init(const device_t *info)
{
    cs423x_t *dev = calloc(1, sizeof(cs423x_t));

    /* Initialize model-specific data. */
    dev->type = info->local & 0xff;
    cs423x_log("CS423x: init(%02X)\n", dev->type);
    switch (dev->type) {
        case CRYSTAL_CS4236:
        case CRYSTAL_CS4236B:
        case CRYSTAL_CS4237B:
        case CRYSTAL_CS4238B:
        case CRYSTAL_CS4235:
        case CRYSTAL_CS4239:
            /* Different WSS codec families. */
            dev->ad1848_type = (dev->type >= CRYSTAL_CS4235) ? AD1848_TYPE_CS4235 : ((dev->type >= CRYSTAL_CS4236B) ? AD1848_TYPE_CS4236B : AD1848_TYPE_CS4236);

            /* Different Chip Version and ID values (N/A on CS4236), which shouldn't be reset by ad1848_init. */
            dev->ad1848.xregs[25] = dev->type;

            /* Same EEPROM structure. */
            dev->pnp_offset = 0x4013;

            if (!(info->local & CRYSTAL_NOEEPROM)) {
                /* Start a new EEPROM with the default configuration data. */
                cs423x_load_defaults(dev, &dev->eeprom_data[4]);

                /* Load PnP resource data ROM. */
                FILE *fp = rom_fopen(PNP_ROM_CS4236B, "rb");
                if (fp) {
                    uint16_t eeprom_pnp_offset = (dev->pnp_offset & 0x1ff) + 4;
                    /* This is wrong. The header field only indicates PnP resource data length, and real chips use
                       it to locate the firmware patch area, but we don't need any of that, so we can get away
                       with pretending the whole ROM is PnP data, at least until we can get full EEPROM dumps. */
                    dev->pnp_size = fread(&dev->eeprom_data[eeprom_pnp_offset], 1, sizeof(dev->eeprom_data) - eeprom_pnp_offset, fp);
                    fclose(fp);
                } else {
                    dev->pnp_size = 0;
                }

                /* Populate EEPROM header if the PnP ROM was loaded. */
                if (dev->pnp_size) {
                    dev->eeprom_data[0] = 0x55;
                    dev->eeprom_data[1] = 0xbb;
                    dev->eeprom_data[2] = dev->pnp_size >> 8;
                    dev->eeprom_data[3] = dev->pnp_size;
                }

                /* Patch PnP ROM and set EEPROM file name. */
                switch (dev->type) {
                    case CRYSTAL_CS4236:
                        if (dev->pnp_size) {
                            dev->eeprom_data[26] = 0x36;
                            dev->eeprom_data[45] = ' ';
                        }
                        dev->nvr_path = "cs4236.nvr";
                        break;

                    case CRYSTAL_CS4236B:
                        dev->nvr_path = "cs4236b.nvr";
                        break;

                    case CRYSTAL_CS4237B:
                        if (dev->pnp_size) {
                            dev->eeprom_data[26] = 0x37;
                            dev->eeprom_data[44] = '7';
                        }
                        dev->nvr_path = "cs4237b.nvr";
                        break;

                    case CRYSTAL_CS4238B:
                        if (dev->pnp_size) {
                            dev->eeprom_data[26] = 0x38;
                            dev->eeprom_data[44] = '8';
                        }
                        dev->nvr_path = "cs4238b.nvr";
                        break;

                    case CRYSTAL_CS4235:
                        if (dev->pnp_size) {
                            dev->eeprom_data[26] = 0x25;
                            dev->eeprom_data[44] = '5';
                            dev->eeprom_data[45] = ' ';
                        }
                        dev->nvr_path = "cs4235.nvr";
                        break;

                    case CRYSTAL_CS4239:
                        if (dev->pnp_size) {
                            dev->eeprom_data[26] = 0x29;
                            dev->eeprom_data[44] = '9';
                            dev->eeprom_data[45] = ' ';
                        }
                        dev->nvr_path = "cs4239.nvr";
                        break;

                    default:
                        break;
                }

                /* Load EEPROM contents from file if present. */
                cs423x_nvram(dev, 0);
            }

            /* Initialize game port. The game port on all B chips only
               responds to 6 I/O ports; the remaining 2 are reserved. */
            dev->gameport = gameport_add((dev->ad1848_type == CRYSTAL_CS4236B) ? &gameport_pnp_6io_device : &gameport_pnp_device);

            break;

        default:
            break;
    }

    /* Initialize I2C bus for the EEPROM. */
    dev->i2c = i2c_gpio_init("nvr_cs423x");

    /* Initialize I2C EEPROM if enabled. */
    if (!(info->local & CRYSTAL_NOEEPROM))
        dev->eeprom = i2c_eeprom_init(i2c_gpio_get_bus(dev->i2c), 0x50, dev->eeprom_data, sizeof(dev->eeprom_data), 1);

    /* Initialize ISAPnP. */
    dev->pnp_card = isapnp_add_card(NULL, 0, cs423x_pnp_config_changed, NULL, NULL, NULL, dev);

    /* Initialize SBPro codec. The WSS codec is initialized later by cs423x_reset */
    dev->sb = device_add_inst(&sb_pro_compat_device, 1);
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, dev->sb); /* CD audio filter for the default context */

    /* Initialize RAM, registers and WSS codec. */
    cs423x_reset(dev);
    sound_add_handler(cs423x_get_buffer, dev);
    music_add_handler(cs423x_get_music_buffer, dev);

    /* Add Control/RAM backdoor handlers for CS4235. */
    dev->ad1848.cram_priv  = dev;
    dev->ad1848.cram_read  = cs423x_read;
    dev->ad1848.cram_write = cs423x_write;

    return dev;
}

static void
cs423x_close(void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;

    cs423x_log("CS423x: close()\n");

    /* Save EEPROM contents to file. */
    if (dev->eeprom) {
        cs423x_nvram(dev, 1);
        i2c_eeprom_close(dev->eeprom);
    }

    i2c_gpio_close(dev->i2c);

    free(dev);
}

static int
cs423x_available(void)
{
    return rom_present(PNP_ROM_CS4236B);
}

static void
cs423x_speed_changed(void *priv)
{
    cs423x_t *dev = (cs423x_t *) priv;

    ad1848_speed_changed(&dev->ad1848);
}

const device_t cs4235_device = {
    .name          = "Crystal CS4235",
    .internal_name = "cs4235",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = CRYSTAL_CS4235,
    .init          = cs423x_init,
    .close         = cs423x_close,
    .reset         = cs423x_reset,
    .available     = cs423x_available,
    .speed_changed = cs423x_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t cs4235_onboard_device = {
    .name          = "Crystal CS4235 (On-Board)",
    .internal_name = "cs4235_onboard",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = CRYSTAL_CS4235 | CRYSTAL_NOEEPROM,
    .init          = cs423x_init,
    .close         = cs423x_close,
    .reset         = cs423x_reset,
    .available     = cs423x_available,
    .speed_changed = cs423x_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t cs4236_onboard_device = {
    .name          = "Crystal CS4236 (On-Board)",
    .internal_name = "cs4236_onboard",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = CRYSTAL_CS4236 | CRYSTAL_NOEEPROM,
    .init          = cs423x_init,
    .close         = cs423x_close,
    .reset         = cs423x_reset,
    .available     = cs423x_available,
    .speed_changed = cs423x_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t cs4236b_device = {
    .name          = "Crystal CS4236B",
    .internal_name = "cs4236b",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = CRYSTAL_CS4236B,
    .init          = cs423x_init,
    .close         = cs423x_close,
    .reset         = cs423x_reset,
    .available     = cs423x_available,
    .speed_changed = cs423x_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t cs4236b_onboard_device = {
    .name          = "Crystal CS4236B",
    .internal_name = "cs4236b",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = CRYSTAL_CS4236B | CRYSTAL_NOEEPROM,
    .init          = cs423x_init,
    .close         = cs423x_close,
    .reset         = cs423x_reset,
    .available     = cs423x_available,
    .speed_changed = cs423x_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t cs4237b_device = {
    .name          = "Crystal CS4237B",
    .internal_name = "cs4237b",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = CRYSTAL_CS4237B,
    .init          = cs423x_init,
    .close         = cs423x_close,
    .reset         = cs423x_reset,
    .available     = cs423x_available,
    .speed_changed = cs423x_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t cs4238b_device = {
    .name          = "Crystal CS4238B",
    .internal_name = "cs4238b",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = CRYSTAL_CS4238B,
    .init          = cs423x_init,
    .close         = cs423x_close,
    .reset         = cs423x_reset,
    .available     = cs423x_available,
    .speed_changed = cs423x_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
