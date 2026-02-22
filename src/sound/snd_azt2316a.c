/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Aztech Sound Galaxy series audio controller emulation.
 *
 * Authors: Cacodemon345
 *          Eluan Costa Miranda <eluancm@gmail.com>
 *          win2kgamer
 *
 *          Copyright 2022 Cacodemon345.
 *          Copyright 2020 Eluan Costa Miranda.
 *          Copyright 2025-2026 win2kgamer
 */

/*
 * Original header and notes:
 *
 * TYPE 0x11: (Washington)
 * Aztech MMPRO16AB,
 * Aztech Sound Galaxy Pro 16 AB
 * Aztech Sound Galaxy Washington 16
 * ...and other OEM names
 * FCC ID I38-MMSN824 and others
 *
 * TYPE 0x0C: (Clinton)
 * Packard Bell FORTE16
 * Aztech Sound Galaxy Nova 16 Extra
 * Aztech Sound Galaxy Clinton 16
 * ...and other OEM names
 *
 * Also works more or less for drivers of other models with the same chipsets.
 *
 * Copyright (c) 2020 Eluan Costa Miranda <eluancm@gmail.com> All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * =============================================================================
 *
 * The CS4248 DSP used is pin and software compatible with the AD1848.
 * I also have one of these cards with a CS4231. The driver talks to the
 * emulated card as if it was a CS4231 and I still don't know how to tell the
 * drivers to see the CS4248. The CS4231 more advanced features are NOT used,
 * just the new input/output channels. Apparently some drivers are hardcoded
 * for one or the other, so there is an option for this.
 *
 * There is lots more to be learned form the Win95 drivers. The Linux driver is
 * very straightforward and doesn't do much.
 *
 * Recording and voice modes in the windows mixer still do nothing in 86Box, so
 * this is missing.
 *
 * There is a jumper to load the startup configuration from an EEPROM. This is
 * implemented, so any software-configured parameters will be saved.
 *
 * The CD-ROM interface commands are just ignored, along with gameport.
 * The MPU401 is always enabled.
 * The OPL3 is always active in some (all?) drivers/cards, so there is no
 * configuration for this.
 *
 * Tested with DOS (driver installation, tools, diagnostics), Win3.1 (driver
 * installation, tools), Win95 (driver auto-detection), lots of games.
 *
 * I consider type 0x11 (Washington) to be very well tested. Type 0x0C (Clinton)
 * wasn't fully tested, but works well for WSS/Windows. BEWARE that there are
 * *too many* driver types and OEM versions for each card. Maybe yours isn't
 * emulated or you have the wrong driver. Some features of each card may work
 * when using wrong drivers. CODEC selection is also important.
 *
 * Any updates to the WSS and SBPROV2 sound cards should be synced here when
 * appropriate. The WSS was completely cloned here, while the SBPROV2 tends
 * to call the original functions, except for initialization.
 *
 * TODO/Notes:
 * -Some stuff still not understood on I/O address 0x624.
 * -Is the CS42xx dither mode used anywhere? Implement.
 * -What are the voice commands mode in Win95 mixer again?
 * -Configuration options not present on Aztech's CONFIG.EXE have been commented
 *  out or deleted. Other types of cards with this chipset may need them.
 * -Sfademo on the Descent CD fails under Win95, works under DOS, see if it
 *  happens on real hardware (and OPL3 stops working after the failure)
 * -There appears to be some differences in sound volumes bertween MIDI,
 *  SBPROV2, WSS and OPL3? Also check relationship between the SBPROV2 mixer and
 *  the WSS mixer! Are they independent? Current mode selects which mixer? Are
 *  they entangled?
 * -Check real hardware to see if advanced, mic boost, etc appear in the mixer?
 * -CD-ROM driver shipped with the card (SGIDECD.SYS) checks for model strings.
 *  I have implemented mine (Aztech CDA 468-02I 4x) in PCem.
 * -Descent 2 W95 version can't start cd music. Happens on real hardware.
 *  Explanation further below.
 * -DOSQuake and Descent 2 DOS cd music do not work under Win95. The mode
 *  selects get truncated and send all zeros for output channel selection and
 *  volume, Descent 2 also has excess zeros! This is a PCem bug, happens on all
 *  sound cards. CD audio works in Winquake and Descent 2 DOS setup program.
 * -DOSQuake CD audio works under DOS with VIDE-CDD.SYS and SGIDECD.SYS.
 *  Descent 2 DOS is still mute but volume selection appears to be working.
 *  Descent 2 fails to launch with SGIDECD.SYS with "Device failed to request
 *  command". SGIDECD.SYS is the CD-ROM driver included with the sound card
 *  drivers. My real CD-ROM drive can't read anything so I can't check the
 *  real behavior of this driver.
 * -Some cards just have regular IDE ports while other have proprietary ports.
 *  The regular IDE ports just have a option to enable an almost-generic CD-ROM
 *  driver in CONFIG.SYS/AUTOEXEC.BAT (like SGIDECD.SYS) and the onboard port
 *  is enabled/disabled by jumpers. The proprietary ones also have
 *  address/dma/irq settings. Since the configuration options are ignored here,
 *  this behaves like a card with a regular interface disabled by jumper and
 *  the configuration just adds/removes the drivers (which will see other IDE
 *  interfaces present) from the boot process.
 * -Search for TODO in this file. :-)
 *
 * Misc things I use to test for regressions: Windows sounds, Descent under
 * dos/windows, Descent 2 dos/windows (+ cd music option), Descent 2 W95 + cd
 * music, Age of Empires (CD + Midi), cd-audio under Windows + volume,
 * cd-audio under dos + volume, Aztech diagnose.exe, Aztech volset /M:3 then
 * volset /D, Aztech setmode, mixer (volume + balance) under dos and windows,
 * DOSQuake under dos and windows (+ cd music and volumes, + Winquake).
 *
 * Reason for Descent 2 Win95 CD-Audio not working:
 * The game calls auxGetNumDevs() to check if any of the AUX devices has
 * caps.wTechnology == AUXCAPS_CDAUDIO, but this fails because the Aztech
 * Win95 driver only returns a "Line-In" device. I'm not aware of any other
 * game that does this and this is completely unnecessary. Other games that
 * play cd audio correctly have the exact *same* initialization code, minus
 * this check that only Descent 2 Win95 does. It would work if it just skipped
 * this check and progressed with calling mciSendCommand() with
 * mciOpenParms.lpstrDeviceType = "cdaudio", like other games do. There are
 * some sound cards listed as incompatible in the game's README.TXT file that
 * are probably due to this.
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
#include <86box/io.h>
#include <86box/midi.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/gameport.h>
#include <86box/snd_ad1848.h>
#include <86box/snd_azt2316a.h>
#include <86box/snd_sb.h>
#include <86box/plat_unused.h>
#include <86box/log.h>

#ifdef ENABLE_AZTECH_LOG
int aztech_do_log = ENABLE_AZTECH_LOG;

static void
aztech_log(void *priv, const char *fmt, ...)
{
    if (aztech_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define aztech_log(fmt, ...)
#endif

/*530, 11, 3 - 530=23*/
/*530, 11, 1 - 530=22*/
/*530, 11, 0 - 530=21*/
/*530, 10, 1 - 530=1a*/
/*530, 10, 0 - 530=19*/
/*530, 9,  1 - 530=12*/
/*530, 7,  1 - 530=0a*/
/*604, 11, 1 - 530=22*/
/*e80, 11, 1 - 530=22*/
/*f40, 11, 1 - 530=22*/

static int azt2316a_wss_dma[4] = { 0, 0, 1, 3 };
static int azt2316a_wss_irq[8] = { 5, 7, 9, 10, 11, 12, 14, 15 }; /* W95 only uses 7-10, others may be wrong */
#if 0
static uint16_t azt2316a_wss_addr[4] = {0x530, 0x604, 0xe80, 0xf40};
#endif

static double aztpr16_vols_5bits[32];

typedef struct azt2316a_t {
    int type;

    uint8_t wss_config;

    uint16_t cur_addr;
    uint16_t cur_wss_addr;
    uint16_t cur_mpu401_addr;

    int cur_irq, cur_dma;
    int cur_wss_enabled;
    int cur_wss_irq;
    int cur_wss_dma;
    int cur_wss_dma16; /* AZTPR16 16-bit DMA */
    int cur_mpu401_irq;
    int cur_mpu401_enabled;
    int gameport_enabled;
    void *gameport;

    uint32_t config_word;
    uint32_t config_word_unlocked;

    uint8_t cur_mode;

    ad1848_t ad1848;
    mpu_t   *mpu;

    sb_t *sb;

    void * log; /* New logging system */
} azt2316a_t;

void
aztpr16_update_mixer(void *priv)
{
    azt2316a_t *azt2316a = (azt2316a_t *) priv;

    aztech_log(azt2316a->log, "Aztech AZTPR16 Mixer update\n");

    azt2316a->ad1848.regs[2]  = ((~azt2316a->sb->mixer_sbpro.regs[0xa0]) & 0x1f); /* CD L */
    azt2316a->ad1848.cd_vol_l = aztpr16_vols_5bits[azt2316a->ad1848.regs[2] & 0x1f];
    azt2316a->ad1848.regs[3]  = ((~azt2316a->sb->mixer_sbpro.regs[0xa2]) & 0x1f); /* CD R */
    azt2316a->ad1848.cd_vol_r = aztpr16_vols_5bits[azt2316a->ad1848.regs[3] & 0x1f];
    azt2316a->ad1848.regs[4]  = ((~azt2316a->sb->mixer_sbpro.regs[0x8c]) & 0x1f); /* FM L */
    azt2316a->ad1848.regs[5]  = ((~azt2316a->sb->mixer_sbpro.regs[0x8e]) & 0x1f); /* FM R */
    azt2316a->ad1848.regs[6]  = (((~azt2316a->sb->mixer_sbpro.regs[0x84]) & 0x3f) >> 1); /* Master L */
    azt2316a->ad1848.regs[7]  = (((~azt2316a->sb->mixer_sbpro.regs[0x86]) & 0x3f) >> 1); /* Master R */

}

static void
azt1605_filter_opl(void *priv, double *out_l, double *out_r)
{
    azt2316a_t *azt2316a = (azt2316a_t *) priv;

    ad1848_filter_channel((void *) &azt2316a->ad1848, AD1848_AUX2, out_l, out_r);
}

static uint8_t
azt2316a_wss_read(uint16_t addr, void *priv)
{
    const azt2316a_t *azt2316a = (azt2316a_t *) priv;
    uint8_t           temp;

    if (addr & 1)
        temp = 4 | (azt2316a->wss_config & 0x40);
    else
        temp = 4 | (azt2316a->wss_config & 0xC0);

    aztech_log(azt2316a->log, "Aztech WSS: [R] (%04X) = %02X\n", addr, temp);
    return temp;
}

static void
azt2316a_wss_write(uint16_t addr, uint8_t val, void *priv)
{
    azt2316a_t *azt2316a  = (azt2316a_t *) priv;
    int         interrupt = 0;
    uint8_t     oldconfig = azt2316a->wss_config;

    aztech_log(azt2316a->log, "Aztech WSS: [W] (%04X) = %02X\n", addr, val);

    if ((oldconfig & 0x40) != (val & 0x40)) {
        aztech_log(azt2316a->log, "Aztech WSS: Config IRQ bit changed\n");
        interrupt = 1;
    }

    azt2316a->wss_config  = val;
    azt2316a->cur_wss_dma = azt2316a_wss_dma[val & 3];
    azt2316a->cur_wss_irq = azt2316a_wss_irq[(val >> 3) & 7];
    ad1848_setdma(&azt2316a->ad1848, azt2316a_wss_dma[val & 3]);
    ad1848_setirq(&azt2316a->ad1848, azt2316a_wss_irq[(val >> 3) & 7]);

    if (interrupt) {
        if (azt2316a->wss_config & 0x40) {
            aztech_log(azt2316a->log, "Aztech WSS: Firing config change IRQ\n");
            picint(1 << azt2316a->cur_wss_irq);
        } else {
            aztech_log(azt2316a->log, "Aztech WSS: Clearing config change IRQ\n");
            picintc(1 << azt2316a->cur_wss_irq);
        }
    }
}

/* generate a config word based on current settings */
static void
aztpr16_create_config_word(void *priv)
{
    azt2316a_t *azt2316a = (azt2316a_t *) priv;
    uint32_t    temp     = 0;

    /* not implemented / hardcoded */
    uint8_t cd_type     = 1;
    uint8_t cd_irq      = 0;

    switch (azt2316a->cur_addr) {
        case 0x220:
            // do nothing
            break;
        case 0x240:
            temp += 1 << 0;
            break;
        case 0x260:
            temp += 2 << 0;
            break;
        case 0x280:
            temp += 3 << 0;
            break;
        default:
            break;
    }

    switch (azt2316a->cur_irq) {
        case 2:
            temp += 1 << 8;
            break;
        case 5:
            temp += 1 << 9;
            break;
        case 7:
            temp += 1 << 10;
            break;
        case 10:
            temp += 1 << 11;
            break;

        default:
            break;
    }

    switch (azt2316a->cur_wss_addr) {
        case 0x530:
            // do nothing
            break;
        case 0x604:
            temp += 1 << 16;
            break;
        case 0xE80:
            temp += 2 << 16;
            break;
        case 0xF40:
            temp += 3 << 16;
            break;

        default:
            break;
    }

    if (azt2316a->cur_wss_enabled)
        temp += 1 << 18;

    if (azt2316a->gameport_enabled)
        temp += 1 << 4;

    switch (azt2316a->cur_mpu401_addr) {
        case 0x300:
            // do nothing
            break;
        case 0x330:
            temp += 1 << 2;
            break;

        default:
            break;
    }

    if (azt2316a->cur_mpu401_enabled)
        temp += 1 << 3;

    switch (cd_type) {
        case 0: // disabled
            //do nothing
            temp += 0 << 5;
            break;
        case 1: // panasonic
            temp += 1 << 5;
            break;
        case 2: // mitsumi/sony/aztech
            temp += 2 << 5;
            break;
        case 3: // all enabled
            temp += 3 << 5;
            break;
        case 4: // unused
            temp += 4 << 5;
            break;
        case 5: // unused
            temp += 5 << 5;
            break;
        case 6: // unused
            temp += 6 << 5;
            break;
        case 7: // unused
            temp += 7 << 5;
            break;

        default:
            break;
    }

    switch (azt2316a->cur_dma) {
        case 0:
            temp += 1 << 24;
            break;
        case 1:
            temp += 1 << 25;
            break;
        case 3:
            temp += 1 << 26;
            break;
        default:
            break;
    }

    switch (azt2316a->cur_wss_dma16) {
        case 5:
            temp += 1 << 27;
            break;
        case 6:
            temp += 1 << 28;
            break;
        case 7:
            temp += 1 << 29;
            break;
        default:
            break;
    }

    switch (azt2316a->cur_mpu401_irq) {
        case 2:
            temp += 1 << 12;
            break;
        case 5:
            temp += 1 << 13;
            break;
        case 7:
            temp += 1 << 14;
            break;
        case 10:
            temp += 1 << 15;
            break;

        default:
            break;
    }

    switch (cd_irq) {
        case 0: // disabled
            // do nothing
            break;
        case 11:
            temp += 1 << 20;
            break;
        case 12:
            temp += 1 << 21;
            break;
        case 15:
            temp += 1 << 22;
            break;

        default:
            break;
    }

    azt2316a->config_word = temp;
    aztech_log(azt2316a->log, "Aztech PR16 Config Word Create: %08X\n", temp);
}


static void
azt1605_create_config_word(void *priv)
{
    azt2316a_t *azt2316a = (azt2316a_t *) priv;
    uint32_t    temp     = 0;

    /* not implemented / hardcoded */
    uint8_t cd_type     = 0; /* TODO: see if the cd-rom was originally connected there on the real machines emulated by 86Box (Packard Bell Legend 100CD, Itautec Infoway Multimidia, etc) */
    uint8_t cd_dma8     = -1;
    uint8_t cd_irq      = 0;

    switch (azt2316a->cur_addr) {
        case 0x220:
            // do nothing
            break;
        case 0x240:
            temp += 1 << 0;
            break;
        default:
            break;
    }

    switch (azt2316a->cur_irq) {
        case 9:
            temp += 1 << 8;
            break;
        case 3:
            temp += 1 << 9;
            break;
        case 5:
            temp += 1 << 10;
            break;
        case 7:
            temp += 1 << 11;
            break;

        default:
            break;
    }

    switch (azt2316a->cur_wss_addr) {
        case 0x530:
            // do nothing
            break;
        case 0x604:
            temp += 1 << 16;
            break;
        case 0xE80:
            temp += 2 << 16;
            break;
        case 0xF40:
            temp += 3 << 16;
            break;

        default:
            break;
    }

    if (azt2316a->cur_wss_enabled)
        temp += 1 << 18;

    if (azt2316a->gameport_enabled)
        temp += 1 << 4;

    switch (azt2316a->cur_mpu401_addr) {
        case 0x300:
            // do nothing
            break;
        case 0x330:
            temp += 1 << 2;
            break;

        default:
            break;
    }

    if (azt2316a->cur_mpu401_enabled)
        temp += 1 << 3;

    switch (cd_type) {
        case 0: // disabled
            // do nothing
            break;
        case 1: // panasonic
            temp += 1 << 5;
            break;
        case 2: // mitsumi/sony/aztech
            temp += 2 << 5;
            break;
        case 3: // all enabled
            temp += 3 << 5;
            break;
        case 4: // unused
            temp += 4 << 5;
            break;
        case 5: // unused
            temp += 5 << 5;
            break;
        case 6: // unused
            temp += 6 << 5;
            break;
        case 7: // unused
            temp += 7 << 5;
            break;

        default:
            break;
    }

    switch (cd_dma8) {
        case 0xFF: // -1
            //do nothing
            break;
        case 0:
            temp += 1 << 22;
            break;
        case 1:
            temp += 2 << 22;
            break;
        case 3:
            temp += 3 << 22;
            break;

        default:
            break;
    }

    switch (azt2316a->cur_mpu401_irq) {
        case 9:
            temp += 1 << 12;
            break;
        case 3:
            temp += 1 << 13;
            break;
        case 5:
            temp += 1 << 14;
            break;
        case 7:
            temp += 1 << 15;
            break;

        default:
            break;
    }

    switch (cd_irq) {
        case 0: // disabled
            // do nothing
            break;
        case 11:
            temp += 1 << 19;
            break;
        case 12:
            temp += 1 << 20;
            break;
        case 15:
            temp += 1 << 21;
            break;

        default:
            break;
    }

    azt2316a->config_word = temp;
    aztech_log(azt2316a->log, "Aztech 1605 Config Word Create: %08X\n", temp);
}

static void
azt2316a_create_config_word(void *priv)
{
    azt2316a_t *azt2316a = (azt2316a_t *) priv;
    uint32_t    temp     = 0;

    /* not implemented / hardcoded */
    uint16_t cd_addr     = 0x310;
    uint8_t  cd_type     = 0; /* TODO: see if the cd-rom was originally connected there on the real machines emulated by 86Box (Packard Bell Legend 100CD, Itautec Infoway Multimidia, etc) */
    uint8_t  cd_dma8     = -1;
    uint8_t  cd_dma16    = -1;
    uint8_t  cd_irq      = 15;

    if (azt2316a->type == SB_SUBTYPE_CLONE_AZT1605_0X0C) {
        azt1605_create_config_word(priv);
        return;
    }

    if (azt2316a->type == SB_SUBTYPE_CLONE_AZTPR16_0X09) {
        aztpr16_create_config_word(priv);
        return;
    }

    switch (azt2316a->cur_addr) {
        case 0x220:
            // do nothing
            break;
        case 0x240:
            temp += 1 << 0;
            break;
        default:
            break;
    }

    switch (azt2316a->cur_irq) {
        case 9:
            temp += 1 << 2;
            break;
        case 5:
            temp += 1 << 3;
            break;
        case 7:
            temp += 1 << 4;
            break;
        case 10:
            temp += 1 << 5;
            break;

        default:
            break;
    }

    switch (azt2316a->cur_dma) {
        case 0:
            temp += 1 << 6;
            break;
        case 1:
            temp += 2 << 6;
            break;
        case 3:
            temp += 3 << 6;
            break;

        default:
            break;
    }

    switch (azt2316a->cur_wss_addr) {
        case 0x530:
            // do nothing
            break;
        case 0x604:
            temp += 1 << 8;
            break;
        case 0xE80:
            temp += 2 << 8;
            break;
        case 0xF40:
            temp += 3 << 8;
            break;

        default:
            break;
    }
    if (azt2316a->cur_wss_enabled)
        temp += 1 << 10;
    if (azt2316a->gameport_enabled)
        temp += 1 << 11;
    switch (azt2316a->cur_mpu401_addr) {
        case 0x300:
            // do nothing
            break;
        case 0x330:
            temp += 1 << 12;
            break;

        default:
            break;
    }

    if (azt2316a->cur_mpu401_enabled)
        temp += 1 << 13;

    switch (cd_addr) {
        case 0x310:
            // do nothing
            break;
        case 0x320:
            temp += 1 << 14;
            break;
        case 0x340:
            temp += 2 << 14;
            break;
        case 0x350:
            temp += 3 << 14;
            break;

        default:
            break;
    }
    switch (cd_type) {
        case 0: /* disabled */
            // do nothing
            break;
        case 1: /* panasonic */
            temp += 1 << 16;
            break;
        case 2: /* sony */
            temp += 2 << 16;
            break;
        case 3: /* mitsumi */
            temp += 3 << 16;
            break;
        case 4: /* aztech */
            temp += 4 << 16;
            break;
        case 5: /* unused */
            temp += 5 << 16;
            break;
        case 6: /* unused */
            temp += 6 << 16;
            break;
        case 7: /* unused */
            temp += 7 << 16;
            break;

        default:
            break;
    }

    switch (cd_dma8) {
        case 0xFF: // -1
            //do nothing
            break;
        case 0:
            temp += 1 << 20;
            break;
        case 1:
            temp += 2 << 20;
            break;
        case 3:
            temp += 3 << 20;
            break;

        default:
            break;
    }

    switch (cd_dma16) {
        case 0xFF: // -1
            //do nothing
            break;
        case 5:
            temp += 1 << 22;
            break;
        case 6:
            temp += 2 << 22;
            break;
        case 7:
            temp += 3 << 22;
            break;

        default:
            break;
    }

    switch (azt2316a->cur_mpu401_irq) {
        case 9:
            temp += 1 << 24;
            break;
        case 5:
            temp += 1 << 25;
            break;
        case 7:
            temp += 1 << 26;
            break;
        case 10:
            temp += 1 << 27;
            break;

        default:
            break;
    }

    switch (cd_irq) {
        case 5:
            temp += 1 << 28;
            break;
        case 11:
            temp += 1 << 29;
            break;
        case 12:
            temp += 1 << 30;
            break;
        case 15:
            temp += 1 << 31;
            break;

        default:
            break;
    }

    azt2316a->config_word = temp;
    aztech_log(azt2316a->log, "Aztech 2316 Config Word Create: %08X\n", temp);
}

static uint8_t
azt1605_config_read(uint16_t addr, void *priv)
{
    const azt2316a_t *azt2316a = (azt2316a_t *) priv;
    uint8_t           temp     = 0;

    /* Some WSS config here + config change enable bit
       (setting bit 7 and writing back) */

    if (addr == (azt2316a->cur_addr + 0x404)) {
        /* TODO: what is the real meaning of the read value?
           I got a mention of bit 0x10 for WSS from disassembling the source
           code of the driver, and when playing with the I/O ports on real
           hardware after doing some configuration, but didn't dig into it.
           Bit 0x08 seems to be a busy flag and generates a timeout
           (continuous re-reading when initializing windows 98) */
        temp = azt2316a->cur_mode ? 0x07 : 0x0F;
        if (azt2316a->config_word_unlocked) {
            temp |= 0x80;
        }
    } else {
        /* Rest of config. Bytes 0x00-0x03 are documented in the Linux driver. */
        /* 0x07 causes EMUTSR to resync mixers when nonzero values are written but the mechanism is unknown */
        /* 0x08-0x0F mixer registers are not documented */
        switch (addr & 0x0f) {
            case 0x00:
                temp = azt2316a->config_word & 0xFF;
                break;
            case 0x01:
                temp = (azt2316a->config_word >> 8);
                break;
            case 0x02:
                temp = (azt2316a->config_word >> 16);
                break;
            case 0x03:
                temp = (azt2316a->config_word >> 24);
                break;
            case 0x08: /* SBPro Voice mixer readout */
                temp = azt2316a->sb->mixer_sbpro.regs[0x04];
                break;
            case 0x09: /* SBPro Mic mixer readout */
                temp = azt2316a->sb->mixer_sbpro.regs[0x0A];
                break;
            case 0x0C: /* SBPro Master mixer readout */
                temp = azt2316a->sb->mixer_sbpro.regs[0x22];
                break;
            case 0x0D: /* SBPro FM mixer readout */
                temp = azt2316a->sb->mixer_sbpro.regs[0x26];
                break;
            case 0x0E: /* SBPro CD mixer readout */
                temp = azt2316a->sb->mixer_sbpro.regs[0x28];
                break;
            default:
                temp = 0x00;
                break;
        }
    }

    aztech_log(azt2316a->log, "Aztech 1605 Config Word Read: (%04X) = %02X\n", addr, temp);

    return temp;
}

static uint8_t
azt2316a_config_read(uint16_t addr, void *priv)
{
    const azt2316a_t *azt2316a = (azt2316a_t *) priv;
    uint8_t           temp     = 0;

    /* Some WSS config here + config change enable bit
       (setting bit 7 and writing back) */

    if (addr == (azt2316a->cur_addr + 0x404)) {
        /* TODO: what is the real meaning of the read value?
           I got a mention of bit 0x10 for WSS from disassembling the source
           code of the driver, and when playing with the I/O ports on real
           hardware after doing some configuration, but didn't dig into it.
           Bit 0x08 seems to be a busy flag and generates a timeout
           (continuous re-reading when initializing windows 98) */
        temp = azt2316a->cur_mode ? 0x07 : 0x0F;
        if (azt2316a->config_word_unlocked) {
            temp |= 0x80;
        }
    } else {
        // Rest of config. These are documented in the Linux driver.
        switch (addr & 0x3) {
            case 0:
                temp = azt2316a->config_word & 0xFF;
                break;
            case 1:
                temp = (azt2316a->config_word >> 8);
                break;
            case 2:
                temp = (azt2316a->config_word >> 16);
                break;
            case 3:
                temp = (azt2316a->config_word >> 24);
                break;

            default:
                break;
        }
    }

    aztech_log(azt2316a->log, "Aztech 2316 Config Word Read: (%04X) = %02X\n", addr, temp);

    return temp;
}

static void
aztpr16_config_write(uint16_t addr, uint8_t val, void *priv)
{
    azt2316a_t *azt2316a = (azt2316a_t *) priv;
    uint8_t     temp;

    aztech_log(azt2316a->log, "Aztech PR16 Config Word Write: (%04X) = %02X\n", addr, val);

    if (addr == (azt2316a->cur_addr + 0x404)) {
        if (val & 0x80)
            azt2316a->config_word_unlocked = 1;
        else
            azt2316a->config_word_unlocked = 0;
    } else if (azt2316a->config_word_unlocked) {
        if (val == 0xFF) {
            return;
        }
        switch (addr & 3) {
            case 0:
                azt2316a->config_word = (azt2316a->config_word & 0xFFFFFF00) | val;

                temp = val & 3;
                if (temp == 0)
                    azt2316a->cur_addr = 0x220;
                else if (temp == 1)
                    azt2316a->cur_addr = 0x240;
                else if (temp == 2)
                    azt2316a->cur_addr = 0x260;
                else if (temp == 3)
                    azt2316a->cur_addr = 0x280;

                if (val & 0x4)
                    azt2316a->cur_mpu401_addr = 0x330;
                else
                    azt2316a->cur_mpu401_addr = 0x300;

                if (val & 0x8)
                    azt2316a->cur_mpu401_enabled = 1;
                else
                    azt2316a->cur_mpu401_enabled = 0;

                if (val & 0x10)
                    azt2316a->gameport_enabled = 1;
                else
                    azt2316a->gameport_enabled = 0;
                break;
            case 1:
                azt2316a->config_word = (azt2316a->config_word & 0xFFFF00FF) | (val << 8);

                if (val & 0x1)
                    azt2316a->cur_irq = 2;
                else if (val & 0x2)
                    azt2316a->cur_irq = 5;
                else if (val & 0x4)
                    azt2316a->cur_irq = 7;
                else if (val & 0x8)
                    azt2316a->cur_irq = 10;
                /* else undefined? */

                if (val & 0x10)
                    azt2316a->cur_mpu401_irq = 2;
                else if (val & 0x20)
                    azt2316a->cur_mpu401_irq = 5;
                else if (val & 0x40)
                    azt2316a->cur_mpu401_irq = 7;
                else if (val & 0x80)
                    azt2316a->cur_mpu401_irq = 10;
                /* else undefined? */
                break;
            case 2:
                azt2316a->config_word = (azt2316a->config_word & 0xFF00FFFF) | (val << 16);

                io_removehandler(azt2316a->cur_wss_addr, 0x0004, azt2316a_wss_read, NULL, NULL, azt2316a_wss_write, NULL, NULL, azt2316a);
                io_removehandler(azt2316a->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &azt2316a->ad1848);

                temp = val & 0x3;
                if (temp == 0)
                    azt2316a->cur_wss_addr = 0x530;
                else if (temp == 1)
                    azt2316a->cur_wss_addr = 0x604;
                else if (temp == 2)
                    azt2316a->cur_wss_addr = 0xE80;
                else if (temp == 3)
                    azt2316a->cur_wss_addr = 0xF40;

                io_sethandler(azt2316a->cur_wss_addr, 0x0004, azt2316a_wss_read, NULL, NULL, azt2316a_wss_write, NULL, NULL, azt2316a);
                io_sethandler(azt2316a->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &azt2316a->ad1848);

                /* no actual effect */
                if (val & 0x4)
                    azt2316a->cur_wss_enabled = 1;
                else
                    azt2316a->cur_wss_enabled = 0;
                break;
            case 3:
                azt2316a->config_word = (azt2316a->config_word & 0x00FFFFFF) | (val << 24);

                if (val & 0x01) {
                    azt2316a->cur_dma = 0;
                    azt2316a->cur_wss_dma = 0;
                } else if (val & 0x02) {
                    azt2316a->cur_dma = 1;
                    azt2316a->cur_wss_dma = 1;
                } else if (val & 0x04) {
                    azt2316a->cur_dma = 3;
                    azt2316a->cur_wss_dma = 3;
                }

                if (val & 0x08)
                    azt2316a->cur_wss_dma16 = 5;
                else if (val & 0x10)
                    azt2316a->cur_wss_dma16 = 6;
                else if (val & 0x20)
                    azt2316a->cur_wss_dma16 = 7;
                break;

            default:
                break;
        }
        /* update sbprov2 configs */
        sb_dsp_setaddr(&azt2316a->sb->dsp, azt2316a->cur_addr);
        sb_dsp_setirq(&azt2316a->sb->dsp, azt2316a->cur_irq);
        sb_dsp_setdma8(&azt2316a->sb->dsp, azt2316a->cur_dma);

        mpu401_change_addr(azt2316a->mpu, azt2316a->cur_mpu401_addr);
        mpu401_setirq(azt2316a->mpu, azt2316a->cur_mpu401_irq);

        ad1848_setdma(&azt2316a->ad1848, azt2316a->cur_wss_dma);
        ad1848_setirq(&azt2316a->ad1848, azt2316a->cur_irq);
        azt2316a->cur_wss_irq = azt2316a->cur_irq;

        gameport_remap(azt2316a->gameport, (azt2316a->gameport_enabled) ? 0x200 : 0x00);
    }
}

static void
azt1605_config_write(uint16_t addr, uint8_t val, void *priv)
{
    azt2316a_t *azt2316a = (azt2316a_t *) priv;
    uint8_t     temp;

    aztech_log(azt2316a->log, "Aztech 1605 Config Word Write: (%04X) = %02X\n", addr, val);

    if (addr == (azt2316a->cur_addr + 0x404)) {
        if (val & 0x80)
            azt2316a->config_word_unlocked = 1;
        else
            azt2316a->config_word_unlocked = 0;
    } else if (azt2316a->config_word_unlocked) {
        if (val == 0xFF) {
            return;
        }
        switch (addr & 3) {
            case 0:
                azt2316a->config_word = (azt2316a->config_word & 0xFFFFFF00) | val;

                temp = val & 3;
                if (temp == 0)
                    azt2316a->cur_addr = 0x220;
                else if (temp == 1)
                    azt2316a->cur_addr = 0x240;

                if (val & 0x4)
                    azt2316a->cur_mpu401_addr = 0x330;
                else
                    azt2316a->cur_mpu401_addr = 0x300;

                if (val & 0x8)
                    azt2316a->cur_mpu401_enabled = 1;
                else
                    azt2316a->cur_mpu401_enabled = 0;

                if (val & 0x10)
                    azt2316a->gameport_enabled = 1;
                else
                    azt2316a->gameport_enabled = 0;
                break;
            case 1:
                azt2316a->config_word = (azt2316a->config_word & 0xFFFF00FF) | (val << 8);

                if (val & 0x1)
                    azt2316a->cur_irq = 9;
                else if (val & 0x2)
                    azt2316a->cur_irq = 3;
                else if (val & 0x4)
                    azt2316a->cur_irq = 5;
                else if (val & 0x8)
                    azt2316a->cur_irq = 7;
                /* else undefined? */

                if (val & 0x10)
                    azt2316a->cur_mpu401_irq = 9;
                else if (val & 0x20)
                    azt2316a->cur_mpu401_irq = 3;
                else if (val & 0x40)
                    azt2316a->cur_mpu401_irq = 5;
                else if (val & 0x80)
                    azt2316a->cur_mpu401_irq = 7;
                /* else undefined? */
                break;
            case 2:
                azt2316a->config_word = (azt2316a->config_word & 0xFF00FFFF) | (val << 16);

                io_removehandler(azt2316a->cur_wss_addr, 0x0004, azt2316a_wss_read, NULL, NULL, azt2316a_wss_write, NULL, NULL, azt2316a);
                io_removehandler(azt2316a->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &azt2316a->ad1848);

                temp = val & 0x3;
                if (temp == 0)
                    azt2316a->cur_wss_addr = 0x530;
                else if (temp == 1)
                    azt2316a->cur_wss_addr = 0x604;
                else if (temp == 2)
                    azt2316a->cur_wss_addr = 0xE80;
                else if (temp == 3)
                    azt2316a->cur_wss_addr = 0xF40;

                io_sethandler(azt2316a->cur_wss_addr, 0x0004, azt2316a_wss_read, NULL, NULL, azt2316a_wss_write, NULL, NULL, azt2316a);
                io_sethandler(azt2316a->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &azt2316a->ad1848);

                /* no actual effect */
                if (val & 0x4)
                    azt2316a->cur_wss_enabled = 1;
                else
                    azt2316a->cur_wss_enabled = 0;
                break;
            case 3:
                break;

            default:
                break;
        }
        /* update sbprov2 configs */
        sb_dsp_setaddr(&azt2316a->sb->dsp, azt2316a->cur_addr);
        sb_dsp_setirq(&azt2316a->sb->dsp, azt2316a->cur_irq);
        sb_dsp_setdma8(&azt2316a->sb->dsp, azt2316a->cur_dma);

        mpu401_change_addr(azt2316a->mpu, azt2316a->cur_mpu401_addr);
        mpu401_setirq(azt2316a->mpu, azt2316a->cur_mpu401_irq);

        gameport_remap(azt2316a->gameport, (azt2316a->gameport_enabled) ? 0x200 : 0x00);
    }
}

static void
azt2316a_config_write(uint16_t addr, uint8_t val, void *priv)
{
    azt2316a_t *azt2316a = (azt2316a_t *) priv;
    uint8_t     temp;

    aztech_log(azt2316a->log, "Aztech 2316 Config Word Write: (%04X) = %02X\n", addr, val);

    if (addr == (azt2316a->cur_addr + 0x404)) {
        if (val & 0x80)
            azt2316a->config_word_unlocked = 1;
        else
            azt2316a->config_word_unlocked = 0;
    } else if (azt2316a->config_word_unlocked) {
        if (val == 0xFF) // TODO: check if this still happens on eeprom.sys after having more complete emulation!
            return;
        switch (addr & 3) {
            case 0:
                azt2316a->config_word = (azt2316a->config_word & 0xFFFFFF00) | val;
                temp                  = val & 3;

                if (temp == 0)
                    azt2316a->cur_addr = 0x220;
                else if (temp == 1)
                    azt2316a->cur_addr = 0x240;

                if (val & 0x4)
                    azt2316a->cur_irq = 9;
                else if (val & 0x8)
                    azt2316a->cur_irq = 5;
                else if (val & 0x10)
                    azt2316a->cur_irq = 7;
                else if (val & 0x20)
                    azt2316a->cur_irq = 10;

                temp = (val >> 6) & 3;
                if (temp == 1)
                    azt2316a->cur_dma = 0;
                else if (temp == 2)
                    azt2316a->cur_dma = 1;
                else if (temp == 3)
                    azt2316a->cur_dma = 3;
                break;
            case 1:
                azt2316a->config_word = (azt2316a->config_word & 0xFFFF00FF) | (val << 8);

                io_removehandler(azt2316a->cur_wss_addr, 0x0004, azt2316a_wss_read, NULL, NULL, azt2316a_wss_write, NULL, NULL, azt2316a);
                io_removehandler(azt2316a->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &azt2316a->ad1848);

                temp = val & 0x3;
                if (temp == 0)
                    azt2316a->cur_wss_addr = 0x530;
                else if (temp == 1)
                    azt2316a->cur_wss_addr = 0x604;
                else if (temp == 2)
                    azt2316a->cur_wss_addr = 0xE80;
                else if (temp == 3)
                    azt2316a->cur_wss_addr = 0xF40;

                io_sethandler(azt2316a->cur_wss_addr, 0x0004, azt2316a_wss_read, NULL, NULL, azt2316a_wss_write, NULL, NULL, azt2316a);
                io_sethandler(azt2316a->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &azt2316a->ad1848);

                /* no actual effect */
                if (val & 0x4)
                    azt2316a->cur_wss_enabled = 1;
                else
                    azt2316a->cur_wss_enabled = 0;

                if (val & 0x8)
                    azt2316a->gameport_enabled = 1;
                else
                    azt2316a->gameport_enabled = 0;

                if (val & 0x10)
                    azt2316a->cur_mpu401_addr = 0x330;
                else
                    azt2316a->cur_mpu401_addr = 0x300;

                if (val & 0x20)
                    azt2316a->cur_mpu401_enabled = 1;
                else
                    azt2316a->cur_mpu401_enabled = 0;
                break;
            case 2:
                azt2316a->config_word = (azt2316a->config_word & 0xFF00FFFF) | (val << 16);
                break;
            case 3:
                azt2316a->config_word = (azt2316a->config_word & 0x00FFFFFF) | (val << 24);

                if (val & 0x1)
                    azt2316a->cur_mpu401_irq = 9;
                else if (val & 0x2)
                    azt2316a->cur_mpu401_irq = 5;
                else if (val & 0x4)
                    azt2316a->cur_mpu401_irq = 7;
                else if (val & 0x8)
                    azt2316a->cur_mpu401_irq = 10;
                /* else undefined? */
                break;

            default:
                break;
        }
        /* update sbprov2 configs */
        sb_dsp_setaddr(&azt2316a->sb->dsp, azt2316a->cur_addr);
        sb_dsp_setirq(&azt2316a->sb->dsp, azt2316a->cur_irq);
        sb_dsp_setdma8(&azt2316a->sb->dsp, azt2316a->cur_dma);

        mpu401_change_addr(azt2316a->mpu, azt2316a->cur_mpu401_addr);
        mpu401_setirq(azt2316a->mpu, azt2316a->cur_mpu401_irq);

        gameport_remap(azt2316a->gameport, (azt2316a->gameport_enabled) ? 0x200 : 0x00);
    }
}

/* How it behaves when one or another is activated may affect games auto-detecting (and will also use more of the limited system resources!) */
void
azt2316a_enable_wss(uint8_t enable, void *priv)
{
    azt2316a_t *azt2316a = (azt2316a_t *) priv;

    sound_set_cd_audio_filter(NULL, NULL);

    if (enable) {
        azt2316a->cur_mode = 1;
        sound_set_cd_audio_filter(ad1848_filter_cd_audio, &azt2316a->ad1848);
        azt2316a->sb->opl_mixer = azt2316a;
        azt2316a->sb->opl_mix   = azt1605_filter_opl;
    }
    else {
        azt2316a->cur_mode = 0;
        sound_set_cd_audio_filter(sbpro_filter_cd_audio, azt2316a->sb);
        azt2316a->sb->opl_mixer = NULL;
        azt2316a->sb->opl_mix   = NULL;
    }
}

void
aztpr16_wss_mode(uint8_t mode, void *priv)
{
    azt2316a_t *azt2316a = (azt2316a_t *) priv;

    sound_set_cd_audio_filter(NULL, NULL);

    if (mode) {
        azt2316a->cur_mode = 1;
        sound_set_cd_audio_filter(ad1848_filter_cd_audio, &azt2316a->ad1848);
        azt2316a->sb->opl_mixer = azt2316a;
        azt2316a->sb->opl_mix   = azt1605_filter_opl;
    }
    else {
        azt2316a->cur_mode = 0;
        sound_set_cd_audio_filter(sbpro_filter_cd_audio, azt2316a->sb);
        azt2316a->sb->opl_mixer = NULL;
        azt2316a->sb->opl_mix   = NULL;
    }

    if (mode == 0x03)
        ad1848_setdma(&azt2316a->ad1848, azt2316a->cur_wss_dma16);
    else
        ad1848_setdma(&azt2316a->ad1848, azt2316a->cur_wss_dma);

}

static void
azt2316a_get_buffer(int32_t *buffer, int len, void *priv)
{
    azt2316a_t *azt2316a = (azt2316a_t *) priv;

    /* wss part */
    ad1848_update(&azt2316a->ad1848);
    for (int c = 0; c < len * 2; c++)
        buffer[c] += (azt2316a->ad1848.buffer[c] / 2);

    azt2316a->ad1848.pos = 0;

    /* sbprov2 part */
    sb_get_buffer_sbpro(buffer, len, azt2316a->sb);
}

static void *
azt_init(const device_t *info)
{
    FILE       *fp;
    char       *fn = NULL;
    int         i;
    int         loaded_from_eeprom = 0;
    uint16_t    addr_setting;
    uint8_t     read_eeprom[AZTECH_EEPROM_SIZE];
    azt2316a_t *azt2316a = calloc(1, sizeof(azt2316a_t));

    azt2316a->type = info->local;

    azt2316a->log = log_open("AztechWSS");

    if (azt2316a->type == SB_SUBTYPE_CLONE_AZT1605_0X0C) {
        fn = "azt1605.nvr";
    } else if (azt2316a->type == SB_SUBTYPE_CLONE_AZT2316A_0X11) {
        fn = "azt2316a.nvr";
    } else if (azt2316a->type == SB_SUBTYPE_CLONE_AZTPR16_0X09) {
        fn = "aztpr16.nvr";
    }

    /* config */
    fp = nvr_fopen(fn, "rb");
    if (fp) {
        uint8_t checksum = 0x7f;
        uint8_t saved_checksum;
        size_t  res;

        res = fread(read_eeprom, AZTECH_EEPROM_SIZE, 1, fp);
        for (i = 0; i < AZTECH_EEPROM_SIZE; i++)
            checksum += read_eeprom[i];

        res = fread(&saved_checksum, sizeof(saved_checksum), 1, fp);
        (void) res;

        fclose(fp);

        if (checksum == saved_checksum)
            loaded_from_eeprom = 1;
    }

    if (!loaded_from_eeprom) {
        if (azt2316a->type == SB_SUBTYPE_CLONE_AZT2316A_0X11) {
            read_eeprom[0]  = 0xee; /* SB Voice mixer value */
            read_eeprom[1]  = 0x00; /* SB Mic mixer value (bits 2-0) */
            read_eeprom[2]  = 0x00; /* SB Record Source */
            read_eeprom[3]  = 0xee; /* SB Master mixer value */
            read_eeprom[4]  = 0xee; /* SB FM mixer value */
            read_eeprom[5]  = 0xee; /* SB CD mixer value */
            read_eeprom[6]  = 0x00; /* SB Line mixer value */
            read_eeprom[7]  = 0x00;
            read_eeprom[8]  = 0x00;
            read_eeprom[9]  = 0x00;
            read_eeprom[10] = 0x00;
            read_eeprom[11] = 0x88;
            read_eeprom[12] = 0xbc;
            read_eeprom[13] = 0x00;
            read_eeprom[14] = 0x01;
            read_eeprom[15] = 0x00;
        } else if (azt2316a->type == SB_SUBTYPE_CLONE_AZT1605_0X0C) {
            read_eeprom[0]  = 0x80; /* WSS ADC L mixer value */
            read_eeprom[1]  = 0x80; /* WSS ADC R mixer value */
            read_eeprom[2]  = 0x08; /* WSS AUX1 L mixer value */
            read_eeprom[3]  = 0x08; /* WSS AUX1 R mixer value */
            read_eeprom[4]  = 0x08; /* WSS AUX2 L mixer value */
            read_eeprom[5]  = 0x08; /* WSS AUX2 R mixer value */
            read_eeprom[6]  = 0x08; /* WSS DAC L mixer value */
            read_eeprom[7]  = 0x08; /* WSS DAC R mixer value */
            read_eeprom[8]  = 0x08; /* WSS LINE L mixer value (CS4231) */
            read_eeprom[9]  = 0x08; /* WSS LINE R mixer value (CS4231) */
            read_eeprom[10] = 0x80; /* WSS MIC mixer value (CS4231) */
            read_eeprom[11] = 0x01;
            read_eeprom[12] = 0x1C;
            read_eeprom[13] = 0x14;
            read_eeprom[14] = 0x04;
            read_eeprom[15] = 0xFF; /* SBPro Master volume (EMUTSR) */
        } else if (azt2316a->type == SB_SUBTYPE_CLONE_AZTPR16_0X09) {
            read_eeprom[0]  = 0x00;
            read_eeprom[1]  = 0x00;
            read_eeprom[2]  = 0x00;
            read_eeprom[3]  = 0x3f; /* Master Volume L */
            read_eeprom[4]  = 0x3f; /* Master Volume R */
            read_eeprom[5]  = 0x1f; /* Wave Volume L */
            read_eeprom[6]  = 0x1f; /* Wave Volume R */
            read_eeprom[7]  = 0x1f; /* FM Volume L */
            read_eeprom[8]  = 0x1f; /* FM Volume R */
            read_eeprom[9]  = 0x1f; /* CD Volume L */
            read_eeprom[10] = 0x1f; /* CD Volume R */
            read_eeprom[11] = 0x1f; /* Line Volume L */
            read_eeprom[12] = 0x1f; /* Line Volume R */
            read_eeprom[13] = 0x1f; /* Mic Volume L */
            read_eeprom[14] = 0x1f; /* Mic Volume R */
            read_eeprom[15] = 0x1f; /* WSynth Volume L */
            read_eeprom[16] = 0x1f; /* WSynth Volume R */
            read_eeprom[32] = 0x1c;
            read_eeprom[33] = 0x12;
            read_eeprom[34] = 0x04;
            read_eeprom[35] = 0x02;
        }
    }

    if (azt2316a->type == SB_SUBTYPE_CLONE_AZT2316A_0X11) {
        azt2316a->config_word = read_eeprom[11] | (read_eeprom[12] << 8) | (read_eeprom[13] << 16) | (read_eeprom[14] << 24);

        switch (azt2316a->config_word & (3 << 0)) {
            case 0:
                azt2316a->cur_addr = 0x220;
                break;
            case 1:
                azt2316a->cur_addr = 0x240;
                break;
            default:
                fatal("AZT2316A: invalid sb addr in config word %08X\n", azt2316a->config_word);
        }

        if (azt2316a->config_word & (1 << 2))
            azt2316a->cur_irq = 9;
        else if (azt2316a->config_word & (1 << 3))
            azt2316a->cur_irq = 5;
        else if (azt2316a->config_word & (1 << 4))
            azt2316a->cur_irq = 7;
        else if (azt2316a->config_word & (1 << 5))
            azt2316a->cur_irq = 10;
        else
            fatal("AZT2316A: invalid sb irq in config word %08X\n", azt2316a->config_word);

        switch (azt2316a->config_word & (3 << 6)) {
            case 1 << 6:
                azt2316a->cur_dma = 0;
                break;
            case 2 << 6:
                azt2316a->cur_dma = 1;
                break;
            case 3 << 6:
                azt2316a->cur_dma = 3;
                break;
            default:
                fatal("AZT2316A: invalid sb dma in config word %08X\n", azt2316a->config_word);
        }

        switch (azt2316a->config_word & (3 << 8)) {
            case 0:
                azt2316a->cur_wss_addr = 0x530;
                break;
            case 1 << 8:
                azt2316a->cur_wss_addr = 0x604;
                break;
            case 2 << 8:
                azt2316a->cur_wss_addr = 0xE80;
                break;
            case 3 << 8:
                azt2316a->cur_wss_addr = 0xF40;
                break;
            default:
                fatal("AZT2316A: invalid wss addr in config word %08X\n", azt2316a->config_word);
        }

        if (azt2316a->config_word & (1 << 10))
            azt2316a->cur_wss_enabled = 1;
        else
            azt2316a->cur_wss_enabled = 0;

        if (azt2316a->config_word & (1 << 11))
            azt2316a->gameport_enabled = 1;
        else
            azt2316a->gameport_enabled = 0;

        if (azt2316a->config_word & (1 << 12))
            azt2316a->cur_mpu401_addr = 0x330;
        else
            azt2316a->cur_mpu401_addr = 0x300;

        if (azt2316a->config_word & (1 << 13))
            azt2316a->cur_mpu401_enabled = 1;
        else
            azt2316a->cur_mpu401_enabled = 0;

        if (azt2316a->config_word & (1 << 24))
            azt2316a->cur_mpu401_irq = 9;
        else if (azt2316a->config_word & (1 << 25))
            azt2316a->cur_mpu401_irq = 5;
        else if (azt2316a->config_word & (1 << 26))
            azt2316a->cur_mpu401_irq = 7;
        else if (azt2316a->config_word & (1 << 27))
            azt2316a->cur_mpu401_irq = 10;
        else
            fatal("AZT2316A: invalid mpu401 irq in config word %08X\n", azt2316a->config_word);

        /* these are not present on the EEPROM */
        azt2316a->cur_wss_irq = device_get_config_int("wss_irq");
        azt2316a->cur_wss_dma = device_get_config_int("wss_dma");
        azt2316a->cur_mode    = 0;
    } else if (azt2316a->type == SB_SUBTYPE_CLONE_AZT1605_0X0C) {
        azt2316a->config_word = read_eeprom[12] + (read_eeprom[13] << 8) + (read_eeprom[14] << 16);

        switch (azt2316a->config_word & (3 << 0)) {
            case 0:
                azt2316a->cur_addr = 0x220;
                break;
            case 1:
                azt2316a->cur_addr = 0x240;
                break;
            default:
                fatal("AZT1605: invalid sb addr in config word %08X\n", azt2316a->config_word);
        }

        if (azt2316a->config_word & (1 << 2))
            azt2316a->cur_mpu401_addr = 0x330;
        else
            azt2316a->cur_mpu401_addr = 0x300;

        if (azt2316a->config_word & (1 << 3))
            azt2316a->cur_mpu401_enabled = 1;
        else
            azt2316a->cur_mpu401_enabled = 0;

        if (azt2316a->config_word & (1 << 4))
            azt2316a->gameport_enabled = 1;
        else
            azt2316a->gameport_enabled = 0;

        if (azt2316a->config_word & (1 << 8))
            azt2316a->cur_irq = 9;
        else if (azt2316a->config_word & (1 << 9))
            azt2316a->cur_irq = 3;
        else if (azt2316a->config_word & (1 << 10))
            azt2316a->cur_irq = 5;
        else if (azt2316a->config_word & (1 << 11))
            azt2316a->cur_irq = 7;
        else
            fatal("AZT1605: invalid sb irq in config word %08X\n", azt2316a->config_word);

        if (azt2316a->config_word & (1 << 12))
            azt2316a->cur_mpu401_irq = 9;
        else if (azt2316a->config_word & (1 << 13))
            azt2316a->cur_mpu401_irq = 3;
        else if (azt2316a->config_word & (1 << 14))
            azt2316a->cur_mpu401_irq = 5;
        else if (azt2316a->config_word & (1 << 15))
            azt2316a->cur_mpu401_irq = 7;
        else
            fatal("AZT1605: invalid mpu401 irq in config word %08X\n", azt2316a->config_word);

        switch (azt2316a->config_word & (3 << 16)) {
            case 0:
                azt2316a->cur_wss_addr = 0x530;
                break;
            case 1 << 16:
                azt2316a->cur_wss_addr = 0x604;
                break;
            case 2 << 16:
                azt2316a->cur_wss_addr = 0xE80;
                break;
            case 3 << 16:
                azt2316a->cur_wss_addr = 0xF40;
                break;
            default:
                fatal("AZT1605: invalid wss addr in config word %08X\n", azt2316a->config_word);
        }

        if (azt2316a->config_word & (1 << 18))
            azt2316a->cur_wss_enabled = 1;
        else
            azt2316a->cur_wss_enabled = 0;

        // these are not present on the EEPROM
        azt2316a->cur_dma     = device_get_config_int("sb_dma8");
        azt2316a->cur_wss_irq = device_get_config_int("wss_irq");
        azt2316a->cur_wss_dma = device_get_config_int("wss_dma");
        azt2316a->cur_mode    = 0;
    } else if (azt2316a->type == SB_SUBTYPE_CLONE_AZTPR16_0X09) {
        azt2316a->config_word = read_eeprom[32] + (read_eeprom[33] << 8) + (read_eeprom[34] << 16) + (read_eeprom[35] << 24);
        aztech_log(azt2316a->log, "AZTPR16 Config Word = %08X\n", azt2316a->config_word);

        switch (azt2316a->config_word & (3 << 0)) {
            case 0:
                azt2316a->cur_addr = 0x220;
                break;
            case 1:
                azt2316a->cur_addr = 0x240;
                break;
            case 2:
                azt2316a->cur_addr = 0x260;
                break;
            case 3:
                azt2316a->cur_addr = 0x280;
                break;
            default:
                fatal("AZTPR16: invalid sb addr in config word %08X\n", azt2316a->config_word);
                break;
        }

        if (azt2316a->config_word & (1 << 2))
            azt2316a->cur_mpu401_addr = 0x330;
        else
            azt2316a->cur_mpu401_addr = 0x300;

        if (azt2316a->config_word & (1 << 3))
            azt2316a->cur_mpu401_enabled = 1;
        else
            azt2316a->cur_mpu401_enabled = 0;

        if (azt2316a->config_word & (1 << 4))
            azt2316a->gameport_enabled = 1;
        else
            azt2316a->gameport_enabled = 0;

        if (azt2316a->config_word & (1 << 8))
            azt2316a->cur_irq = 2;
        else if (azt2316a->config_word & (1 << 9))
            azt2316a->cur_irq = 5;
        else if (azt2316a->config_word & (1 << 10))
            azt2316a->cur_irq = 7;
        else if (azt2316a->config_word & (1 << 11))
            azt2316a->cur_irq = 10;
        else
            fatal("AZTPR16: invalid sb irq in config word %08X\n", azt2316a->config_word);

        if (azt2316a->config_word & (1 << 12))
            azt2316a->cur_mpu401_irq = 2;
        else if (azt2316a->config_word & (1 << 13))
            azt2316a->cur_mpu401_irq = 5;
        else if (azt2316a->config_word & (1 << 14))
            azt2316a->cur_mpu401_irq = 7;
        else if (azt2316a->config_word & (1 << 15))
            azt2316a->cur_mpu401_irq = 10;
        else
            fatal("AZTPR16: invalid mpu401 irq in config word %08X\n", azt2316a->config_word);
        switch (azt2316a->config_word & (3 << 16)) {
            case 0:
                azt2316a->cur_wss_addr = 0x530;
                break;
            case 1 << 16:
                azt2316a->cur_wss_addr = 0x604;
                break;
            case 2 << 16:
                azt2316a->cur_wss_addr = 0xE80;
                break;
            case 3 << 16:
                azt2316a->cur_wss_addr = 0xF40;
                break;
            default:
                fatal("AZTPR16: invalid wss addr in config word %08X\n", azt2316a->config_word);
                break;
        }

        if (azt2316a->config_word & (1 << 18))
            azt2316a->cur_wss_enabled = 1;
        else
            azt2316a->cur_wss_enabled = 0;

        if (azt2316a->config_word & (1 << 24))
            azt2316a->cur_dma = 0;
         else if (azt2316a->config_word & (1 << 25))
            azt2316a->cur_dma = 1;
         else if (azt2316a->config_word & (1 << 26))
            azt2316a->cur_dma = 3;

        if (azt2316a->config_word & (1 << 27))
            azt2316a->cur_wss_dma16 = 5;
        else if (azt2316a->config_word & (1 << 28))
            azt2316a->cur_wss_dma16 = 6;
        else if (azt2316a->config_word & (1 << 29))
            azt2316a->cur_wss_dma16 = 7;

        // these are not present on the EEPROM
        azt2316a->cur_wss_irq = azt2316a->cur_irq;
        azt2316a->cur_wss_dma = azt2316a->cur_dma;
        azt2316a->cur_mode    = 0;
    }

    if (azt2316a->type != SB_SUBTYPE_CLONE_AZTPR16_0X09) {
        addr_setting = device_get_config_hex16("addr");
        if (addr_setting)
            azt2316a->cur_addr = addr_setting;
    }

    /* wss part */
    if (azt2316a->type == SB_SUBTYPE_CLONE_AZTPR16_0X09)
        ad1848_init(&azt2316a->ad1848, AD1848_TYPE_DEFAULT); /* AZTPR16 has an internal AD1848-compatible (non-Mode 2 capable) WSS codec */
    else
        ad1848_init(&azt2316a->ad1848, device_get_config_int("codec"));
    if (azt2316a->type == SB_SUBTYPE_CLONE_AZT2316A_0X11)
        ad1848_set_cd_audio_channel(&azt2316a->ad1848, (device_get_config_int("codec") == AD1848_TYPE_CS4248) ? AD1848_AUX1 : AD1848_LINE_IN);
    else
        ad1848_set_cd_audio_channel(&azt2316a->ad1848, AD1848_AUX1);

    ad1848_setirq(&azt2316a->ad1848, azt2316a->cur_wss_irq);
    ad1848_setdma(&azt2316a->ad1848, azt2316a->cur_wss_dma);

    if (azt2316a->type == SB_SUBTYPE_CLONE_AZT2316A_0X11)
        io_sethandler(azt2316a->cur_addr + 0x0400, 0x0040, azt2316a_config_read, NULL, NULL, azt2316a_config_write, NULL, NULL, azt2316a);
    else if (azt2316a->type == SB_SUBTYPE_CLONE_AZTPR16_0X09)
        io_sethandler(azt2316a->cur_addr + 0x0400, 0x0010, azt1605_config_read, NULL, NULL, aztpr16_config_write, NULL, NULL, azt2316a);
    else /* Aztech 1605 only needs 62x/64x */
        io_sethandler(azt2316a->cur_addr + 0x0400, 0x0010, azt1605_config_read, NULL, NULL, azt1605_config_write, NULL, NULL, azt2316a);
    io_sethandler(azt2316a->cur_wss_addr, 0x0004, azt2316a_wss_read, NULL, NULL, azt2316a_wss_write, NULL, NULL, azt2316a);
    io_sethandler(azt2316a->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &azt2316a->ad1848);

    /* sbprov2 part */
    /*sbpro port mappings. 220h or 240h.
      2x0 to 2x3 -> FM chip (18 voices)
      2x4 to 2x5 -> Mixer interface
      2x6, 2xA, 2xC, 2xE -> DSP chip
      2x8, 2x9, 388 and 389 FM chip (9 voices).*/
    azt2316a->sb = calloc(1, sizeof(sb_t));

    azt2316a->sb->opl_enabled = device_get_config_int("opl");

    for (i = 0; i < AZTECH_EEPROM_SIZE; i++)
        azt2316a->sb->dsp.azt_eeprom[i] = read_eeprom[i];

    if (azt2316a->sb->opl_enabled)
        fm_driver_get(FM_YMF262, &azt2316a->sb->opl);

    sb_dsp_set_real_opl(&azt2316a->sb->dsp, 1);
    sb_dsp_init(&azt2316a->sb->dsp, SBPRO_DSP_302, azt2316a->type, azt2316a);
    sb_dsp_setaddr(&azt2316a->sb->dsp, azt2316a->cur_addr);
    sb_dsp_setirq(&azt2316a->sb->dsp, azt2316a->cur_irq);
    sb_dsp_setdma8(&azt2316a->sb->dsp, azt2316a->cur_dma);
    sb_ct1345_mixer_reset(azt2316a->sb);
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    if (azt2316a->sb->opl_enabled) {
        io_sethandler(azt2316a->cur_addr + 0, 0x0004, azt2316a->sb->opl.read, NULL, NULL, azt2316a->sb->opl.write, NULL, NULL, azt2316a->sb->opl.priv);
        io_sethandler(azt2316a->cur_addr + 8, 0x0002, azt2316a->sb->opl.read, NULL, NULL, azt2316a->sb->opl.write, NULL, NULL, azt2316a->sb->opl.priv);
        io_sethandler(0x0388, 0x0004, azt2316a->sb->opl.read, NULL, NULL, azt2316a->sb->opl.write, NULL, NULL, azt2316a->sb->opl.priv);
    }

    io_sethandler(azt2316a->cur_addr + 4, 0x0002, sb_ct1345_mixer_read, NULL, NULL, sb_ct1345_mixer_write, NULL, NULL, azt2316a->sb);

    azt2316a_create_config_word(azt2316a);
    sound_add_handler(azt2316a_get_buffer, azt2316a);

    if (azt2316a->type == SB_SUBTYPE_CLONE_AZT2316A_0X11) {
        if (azt2316a->sb->opl_enabled)
            music_add_handler(sb_get_music_buffer_sbpro, azt2316a->sb);
    }
    else {
        if (azt2316a->sb->opl_enabled) {
            azt2316a->sb->opl_mixer = azt2316a;
            azt2316a->sb->opl_mix = azt1605_filter_opl;
            music_add_handler(sb_get_music_buffer_sbpro, azt2316a->sb);
        }
    }
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, azt2316a->sb);

    if (azt2316a->cur_mpu401_enabled) {
        azt2316a->mpu = (mpu_t *) calloc(1, sizeof(mpu_t));
        mpu401_init(azt2316a->mpu, azt2316a->cur_mpu401_addr, azt2316a->cur_mpu401_irq, M_UART, device_get_config_int("receive_input401"));
    } else
        azt2316a->mpu = NULL;

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &azt2316a->sb->dsp);

    /* Restore SBPro mixer settings from EEPROM on AZT2316A cards */
    if (azt2316a->type == SB_SUBTYPE_CLONE_AZT2316A_0X11) {
        azt2316a->sb->mixer_sbpro.regs[0x04] = read_eeprom[0]; /* SBPro Voice */
        azt2316a->sb->mixer_sbpro.regs[0x0a] = read_eeprom[1]; /* SBPro Mic */
        azt2316a->sb->mixer_sbpro.regs[0x0c] = read_eeprom[2]; /* SBPro Record Source */
        azt2316a->sb->mixer_sbpro.regs[0x22] = read_eeprom[3]; /* SBPro Master */
        azt2316a->sb->mixer_sbpro.regs[0x26] = read_eeprom[4]; /* SBPro FM */
        azt2316a->sb->mixer_sbpro.regs[0x28] = read_eeprom[5]; /* SBPro CD */
        azt2316a->sb->mixer_sbpro.regs[0x2e] = read_eeprom[6]; /* SBPro Line */
    }

    /* Restore WSS mixer settings from EEPROM on AZT1605 cards */
    if (azt2316a->type == SB_SUBTYPE_CLONE_AZT1605_0X0C) {
        azt2316a->ad1848.regs[0]  = read_eeprom[0];  /* WSS ADC L */
        azt2316a->ad1848.regs[1]  = read_eeprom[1];  /* WSS ADC R */
        azt2316a->ad1848.regs[2]  = read_eeprom[2];  /* WSS AUX1/CD L */
        azt2316a->ad1848.regs[3]  = read_eeprom[3];  /* WSS AUX1/CD R */
        azt2316a->ad1848.regs[4]  = read_eeprom[4];  /* WSS AUX2/FM L */
        azt2316a->ad1848.regs[5]  = read_eeprom[5];  /* WSS AUX2/FM R */
        azt2316a->ad1848.regs[6]  = read_eeprom[6];  /* WSS DAC L */
        azt2316a->ad1848.regs[7]  = read_eeprom[7];  /* WSS DAC R */
        azt2316a->ad1848.regs[18] = read_eeprom[8];  /* CS4231 LINE/SB Voice L */
        azt2316a->ad1848.regs[19] = read_eeprom[9];  /* CS4231 LINE/SB Voice R */
        azt2316a->ad1848.regs[26] = read_eeprom[10]; /* CS4231 Mic */
    }
    /* Restore mixer settings from EEPROM on AZTPR16 cards */
    if (azt2316a->type == SB_SUBTYPE_CLONE_AZTPR16_0X09) {
        azt2316a->sb->mixer_sbpro.regs[0x84] = read_eeprom[3]; /* Master L */
        azt2316a->sb->mixer_sbpro.regs[0x86] = read_eeprom[4]; /* Master R */
        azt2316a->sb->mixer_sbpro.regs[0x88] = read_eeprom[5]; /* Wave L */
        azt2316a->sb->mixer_sbpro.regs[0x8a] = read_eeprom[6]; /* Wave R */
        azt2316a->sb->mixer_sbpro.regs[0x8c] = read_eeprom[7]; /* FM L */
        azt2316a->sb->mixer_sbpro.regs[0x8e] = read_eeprom[8]; /* FM R */
        azt2316a->sb->mixer_sbpro.regs[0xa0] = read_eeprom[9]; /* CD L */
        azt2316a->sb->mixer_sbpro.regs[0xa2] = read_eeprom[10]; /* CD R */
        azt2316a->sb->mixer_sbpro.regs[0xa4] = read_eeprom[11]; /* Line L */
        azt2316a->sb->mixer_sbpro.regs[0xa6] = read_eeprom[12]; /* Line R */
        azt2316a->sb->mixer_sbpro.regs[0xa8] = read_eeprom[13]; /* Mic L */
        azt2316a->sb->mixer_sbpro.regs[0xaa] = read_eeprom[14]; /* Mic R */
        azt2316a->sb->mixer_sbpro.regs[0xac] = read_eeprom[15]; /* WSynth L */
        azt2316a->sb->mixer_sbpro.regs[0xae] = read_eeprom[16]; /* WSynth R */
        azt2316a->sb->mixer_sbpro.regs[0xc2] = read_eeprom[18]; /* Speaker */
        azt2316a->sb->mixer_sbpro.regs[0xc4] = read_eeprom[19]; /* Bass */
        azt2316a->sb->mixer_sbpro.regs[0xc6] = read_eeprom[20]; /* Treble */
        azt2316a->sb->mixer_sbpro.regs[0xc8] = read_eeprom[21]; /* I/O settings byte 1 */
        azt2316a->sb->mixer_sbpro.regs[0xca] = read_eeprom[22]; /* I/O settings byte 2 */
        azt2316a->sb->mixer_sbpro.regs[0xcc] = read_eeprom[23]; /* I/O settings byte 3 */
        azt2316a->sb->mixer_sbpro.regs[0xce] = read_eeprom[24]; /* I/O settings byte 4 */
        azt2316a->sb->mixer_sbpro.regs[0xe0] = read_eeprom[25]; /* Record Gain R */
        azt2316a->sb->mixer_sbpro.regs[0xe2] = read_eeprom[26]; /* Record Gain L */
        azt2316a->sb->mixer_sbpro.regs[0xe4] = read_eeprom[27]; /* Output Gain L */
        azt2316a->sb->mixer_sbpro.regs[0xe6] = read_eeprom[28]; /* Output Gain L */
        azt2316a->sb->mixer_sbpro.regs[0xe8] = read_eeprom[29]; /* Output Gain L */

        /* Sane initial WSS values */
        azt2316a->ad1848.regs[0]  = 0x08;  /* WSS ADC L */
        azt2316a->ad1848.regs[1]  = 0x08;  /* WSS ADC R */
        azt2316a->ad1848.regs[2]  = 0x08;  /* WSS AUX1/CD L */
        azt2316a->ad1848.regs[3]  = 0x08;  /* WSS AUX1/CD R */
        azt2316a->ad1848.regs[4]  = 0x08;  /* WSS AUX2/FM L */
        azt2316a->ad1848.regs[5]  = 0x08;  /* WSS AUX2/FM R */
        azt2316a->ad1848.regs[6]  = 0x08;  /* WSS DAC L */
        azt2316a->ad1848.regs[7]  = 0x08;  /* WSS DAC R */

        /* Set up CD volume table */
        uint8_t c;
        double  attenuation;

        for (c = 0; c < 32; c++) {
            attenuation = 12.0;
            if (c & 0x01)
                attenuation -= 1.5;
            if (c & 0x02)
                attenuation -= 3.0;
            if (c & 0x04)
                attenuation -= 6.0;
            if (c & 0x08)
                attenuation -= 12.0;
            if (c & 0x10)
                attenuation -= 24.0;

            attenuation = pow(10, attenuation / 10);

            aztpr16_vols_5bits[c] = (attenuation * 65536);
        }

        aztpr16_update_mixer(azt2316a);
    }

    azt2316a->gameport = gameport_add(&gameport_pnp_device);
    gameport_remap(azt2316a->gameport, (azt2316a->gameport_enabled) ? 0x200: 0x00);

    return azt2316a;
}

static void
azt_close(void *priv)
{
    azt2316a_t *azt2316a = (azt2316a_t *) priv;
    char       *fn       = NULL;
    FILE       *fp;
    uint8_t     checksum = 0x7f;

    if (azt2316a->type == SB_SUBTYPE_CLONE_AZT1605_0X0C) {
        fn = "azt1605.nvr";
    } else if (azt2316a->type == SB_SUBTYPE_CLONE_AZT2316A_0X11) {
        fn = "azt2316a.nvr";
    } else if (azt2316a->type == SB_SUBTYPE_CLONE_AZTPR16_0X09) {
        fn = "aztpr16.nvr";
    }

    /* always save to eeprom (recover from bad values) */
    fp = nvr_fopen(fn, "wb");
    if (fp) {
        for (uint8_t i = 0; i < AZTECH_EEPROM_SIZE; i++)
            checksum += azt2316a->sb->dsp.azt_eeprom[i];
        fwrite(azt2316a->sb->dsp.azt_eeprom, AZTECH_EEPROM_SIZE, 1, fp);

        // TODO: should remember to save wss duplex setting if 86Box has voice recording implemented in the future? Also, default azt2316a->wss_config
        // TODO: azt2316a->cur_mode is not saved to EEPROM?
        fwrite(&checksum, sizeof(checksum), 1, fp);

        fclose(fp);
    }

    sb_close(azt2316a->sb);

    free(azt2316a->mpu);

    if (azt2316a->log != NULL) {
        log_close(azt2316a->log);
        azt2316a->log = NULL;
    }

    free(azt2316a);
}

static void
azt_speed_changed(void *priv)
{
    azt2316a_t *azt2316a = (azt2316a_t *) priv;

    ad1848_speed_changed(&azt2316a->ad1848);
    sb_speed_changed(azt2316a->sb);
}

static const device_config_t aztpr16_config[] = {
  // clang-format off
    {
        .name           = "opl",
        .description    = "Enable OPL",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "receive_input",
        .description    = "Receive MIDI input",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "receive_input401",
        .description    = "Receive MIDI input (MPU-401)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_config_t azt1605_config[] = {
  // clang-format off
    {
        .name           = "codec",
        .description    = "Codec",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = AD1848_TYPE_CS4248,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "CS4248", .value = AD1848_TYPE_CS4248 },
            { .description = "CS4231", .value = AD1848_TYPE_CS4231 },
            { .description = ""                                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "addr",
        .description    = "SB Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x220",              .value = 0x220 },
            { .description = "0x240",              .value = 0x240 },
            { .description = "Use EEPROM setting", .value =     0 },
            { .description = ""                                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "sb_dma8",
        .description    = "SB low DMA",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "DMA 0", .value = 0 },
            { .description = "DMA 1", .value = 1 },
            { .description = "DMA 3", .value = 3 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "wss_irq",
        .description    = "WSS IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 10,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 11", .value = 11 },
            { .description = "IRQ 10", .value = 10 },
            { .description = "IRQ 7",  .value =  7 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "wss_dma",
        .description    = "WSS DMA",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "DMA 0", .value = 0 },
            { .description = "DMA 1", .value = 1 },
            { .description = "DMA 3", .value = 3 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "opl",
        .description    = "Enable OPL",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "receive_input",
        .description    = "Receive MIDI input",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "receive_input401",
        .description    = "Receive MIDI input (MPU-401)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_config_t azt2316a_config[] = {
  // clang-format off
    {
        .name           = "codec",
        .description    = "Codec",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = AD1848_TYPE_CS4248,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "CS4248", .value = AD1848_TYPE_CS4248 },
            { .description = "CS4231", .value = AD1848_TYPE_CS4231 },
            { .description = ""                                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "addr",
        .description    = "SB Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x220",              .value = 0x220 },
            { .description = "0x240",              .value = 0x240 },
            { .description = "Use EEPROM setting", .value =     0 },
            { .description = ""                                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "wss_irq",
        .description    = "WSS IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 10,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 11", .value = 11 },
            { .description = "IRQ 10", .value = 10 },
            { .description = "IRQ 7",  .value =  7 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "wss_dma",
        .description    = "WSS DMA",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "DMA 0", .value = 0 },
            { .description = "DMA 1", .value = 1 },
            { .description = "DMA 3", .value = 3 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "opl",
        .description    = "Enable OPL",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "receive_input",
        .description    = "Receive MIDI input",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "receive_input401",
        .description    = "Receive MIDI input (MPU-401)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t azt2316a_device = {
    .name          = "Aztech Sound Galaxy Pro 16 AB (Washington)",
    .internal_name = "azt2316a",
    .flags         = DEVICE_ISA16,
    .local         = SB_SUBTYPE_CLONE_AZT2316A_0X11,
    .init          = azt_init,
    .close         = azt_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = azt_speed_changed,
    .force_redraw  = NULL,
    .config        = azt2316a_config
};

const device_t azt1605_device = {
    .name          = "Aztech Sound Galaxy Nova 16 Extra (Clinton)",
    .internal_name = "azt1605",
    .flags         = DEVICE_ISA16,
    .local         = SB_SUBTYPE_CLONE_AZT1605_0X0C,
    .init          = azt_init,
    .close         = azt_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = azt_speed_changed,
    .force_redraw  = NULL,
    .config        = azt1605_config
};

const device_t aztpr16_device = {
    .name          = "Aztech Sound Galaxy Pro 16 (AZTPR16)",
    .internal_name = "aztpr16",
    .flags         = DEVICE_ISA16,
    .local         = SB_SUBTYPE_CLONE_AZTPR16_0X09,
    .init          = azt_init,
    .close         = azt_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = azt_speed_changed,
    .force_redraw  = NULL,
    .config        = aztpr16_config
};
