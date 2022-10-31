/*
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
 * -Some stuff still not understood on I/O addresses 0x624 and 0x530-0x533.
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
 * -Continue reverse engineering to see if the AZT1605 shares the SB DMA with
 *  WSS or if it is set separately by the TSR loaded on boot. Currently it is
 *  only set by PCem config and then saved to EEPROM (which would make it fixed
 *  if loading from EEPROM too).
 * -Related to the previous note, part of the SBPROV2 emulation on the CLINTON
 *  family appears to be implemented with a TSR. It's better to remove the TSR
 *  for now. I need to investigate this. Mixer is also weird under DOS (and
 *  wants to save to EEPROM? I don't think the WASHINGTON does this).
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
#include <86box/snd_ad1848.h>
#include <86box/snd_azt2316a.h>
#include <86box/snd_sb.h>

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
// static uint16_t azt2316a_wss_addr[4] = {0x530, 0x604, 0xe80, 0xf40};

typedef struct azt2316a_t {
    int type;
    int wss_interrupt_after_config;

    uint8_t wss_config;

    uint16_t cur_addr, cur_wss_addr, cur_mpu401_addr;

    int cur_irq, cur_dma;
    int cur_wss_enabled, cur_wss_irq, cur_wss_dma;
    int cur_mpu401_irq;
    int cur_mpu401_enabled;

    uint32_t config_word;
    uint32_t config_word_unlocked;

    uint8_t cur_mode;

    ad1848_t ad1848;
    mpu_t   *mpu;

    sb_t *sb;
} azt2316a_t;

static uint8_t
azt2316a_wss_read(uint16_t addr, void *p)
{
    azt2316a_t *azt2316a = (azt2316a_t *) p;
    uint8_t     temp;

    /* TODO: when windows is initializing, writing 0x48, 0x58 and 0x60 to
       0x530 makes reading from 0x533 return 0x44, but writing 0x50
       makes this return 0x04. Why? */
    if (addr & 1)
        temp = 4 | (azt2316a->wss_config & 0x40);
    else
        temp = 4 | (azt2316a->wss_config & 0xC0);

    return temp;
}

static void
azt2316a_wss_write(uint16_t addr, uint8_t val, void *p)
{
    azt2316a_t *azt2316a  = (azt2316a_t *) p;
    int         interrupt = 0;

    if (azt2316a->wss_interrupt_after_config) {
        if ((azt2316a->wss_config & 0x40) && !(val & 0x40)) { // TODO: is this the right edge?
            interrupt = 1;
        }
    }

    azt2316a->wss_config  = val;
    azt2316a->cur_wss_dma = azt2316a_wss_dma[val & 3];
    azt2316a->cur_wss_irq = azt2316a_wss_irq[(val >> 3) & 7];
    ad1848_setdma(&azt2316a->ad1848, azt2316a_wss_dma[val & 3]);
    ad1848_setirq(&azt2316a->ad1848, azt2316a_wss_irq[(val >> 3) & 7]);

    if (interrupt)
        picint(1 << azt2316a->cur_wss_irq);
}

/* generate a config word based on current settings */
static void
azt1605_create_config_word(void *p)
{
    azt2316a_t *azt2316a = (azt2316a_t *) p;
    uint32_t    temp     = 0;

    /* not implemented / hardcoded */
    uint8_t game_enable = 1;
    uint8_t cd_type     = 0; /* TODO: see if the cd-rom was originally connected there on the real machines emulated by 86Box (Packard Bell Legend 100CD, Itautec Infoway Multimidia, etc) */
    uint8_t cd_dma8     = -1;
    uint8_t cd_irq      = 0;

    switch (azt2316a->cur_addr) {
        case 0x220:
            /* do nothing
            temp += 0 << 0; */
            break;
        case 0x240:
            temp += 1 << 0;
            break;
            /*
                    case 0x260: // TODO: INVALID?
                        temp += 2 << 0;
                        break;
                    case 0x280: // TODO: INVALID?
                        temp += 3 << 0;
                        break;
            */
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
    }

    switch (azt2316a->cur_wss_addr) {
        case 0x530:
            /* do nothing
            temp += 0 << 16; */
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
    }

    if (azt2316a->cur_wss_enabled)
        temp += 1 << 18;

    if (game_enable)
        temp += 1 << 4;

    switch (azt2316a->cur_mpu401_addr) {
        case 0x300:
            /* do nothing
            temp += 0 << 2; */
            break;
        case 0x330:
            temp += 1 << 2;
            break;
    }

    if (azt2316a->cur_mpu401_enabled)
        temp += 1 << 3;

    switch (cd_type) {
        case 0: /* disabled
            do nothing
            temp += 0 << 5; */
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
    }

    switch (cd_dma8) {
        case 0xFF: /* -1
            do nothing
            temp += 0 << 22;*/
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
    }

    azt2316a->config_word = temp;
}

static void
azt2316a_create_config_word(void *p)
{
    azt2316a_t *azt2316a = (azt2316a_t *) p;
    uint32_t    temp     = 0;

    /* not implemented / hardcoded */
    uint8_t  game_enable = 1;
    uint16_t cd_addr     = 0x310;
    uint8_t  cd_type     = 0; /* TODO: see if the cd-rom was originally connected there on the real machines emulated by 86Box (Packard Bell Legend 100CD, Itautec Infoway Multimidia, etc) */
    uint8_t  cd_dma8     = -1;
    uint8_t  cd_dma16    = -1;
    uint8_t  cd_irq      = 15;

    if (azt2316a->type == SB_SUBTYPE_CLONE_AZT1605_0X0C) {
        azt1605_create_config_word(p);
        return;
    }

    switch (azt2316a->cur_addr) {
        case 0x220:
            /* do nothing
            temp += 0 << 0; */
            break;
        case 0x240:
            temp += 1 << 0;
            break;
            /*
                    case 0x260: // TODO: INVALID?
                        temp += 2 << 0;
                        break;
                    case 0x280: // TODO: INVALID?
                        temp += 3 << 0;
                        break;
            */
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
    }

    switch (azt2316a->cur_dma) {
            /*
                    // TODO: INVALID?
                    case 0xFF: // -1
                        // do nothing
                        //temp += 0 << 6;
                        break;
            */
        case 0:
            temp += 1 << 6;
            break;
        case 1:
            temp += 2 << 6;
            break;
        case 3:
            temp += 3 << 6;
            break;
    }

    switch (azt2316a->cur_wss_addr) {
        case 0x530:
            // do nothing
            // temp += 0 << 8;
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
    }
    if (azt2316a->cur_wss_enabled)
        temp += 1 << 10;
    if (game_enable)
        temp += 1 << 11;
    switch (azt2316a->cur_mpu401_addr) {
        case 0x300:
            // do nothing
            // temp += 0 << 12;
            break;
        case 0x330:
            temp += 1 << 12;
            break;
    }

    if (azt2316a->cur_mpu401_enabled)
        temp += 1 << 13;

    switch (cd_addr) {
        case 0x310:
            // do nothing
            // temp += 0 << 14;
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
    }
    switch (cd_type) {
        case 0: /* disabled
            do nothing
            temp += 0 << 16; */
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
    }

    switch (cd_dma8) {
        case 0xFF: /* -1
            do nothing
            temp += 0 << 20; */
            break;
        case 0:
            temp += 1 << 20;
            break;
            /*
                    case 1: // TODO: INVALID?
                        temp += 2 << 20;
                        break;
            */
        case 3:
            temp += 3 << 20;
            break;
    }

    switch (cd_dma16) {
        case 0xFF: /* -1
            do nothing
            temp += 0 << 22; */
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
    }

    azt2316a->config_word = temp;
}

static uint8_t
azt2316a_config_read(uint16_t addr, void *p)
{
    azt2316a_t *azt2316a = (azt2316a_t *) p;
    uint8_t     temp     = 0;

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
        }
    }

    return temp;
}

static void
azt1605_config_write(uint16_t addr, uint8_t val, void *p)
{
    azt2316a_t *azt2316a = (azt2316a_t *) p;
    uint8_t     temp;

    if (addr == (azt2316a->cur_addr + 0x404)) {
        if (val & 0x80)
            azt2316a->config_word_unlocked = 1;
        else
            azt2316a->config_word_unlocked = 0;
    } else if (azt2316a->config_word_unlocked) {
        if (val == 0xFF) { /* TODO: check if this still happens on eeprom.sys after having more complete emulation! */
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
                /*
                                else if (temp == 2)
                                    azt2316a->cur_addr = 0x260; // TODO: INVALID
                                else if (temp == 3)
                                    azt2316a->cur_addr = 0x280; // TODO: INVALID
                */
                if (val & 0x4)
                    azt2316a->cur_mpu401_addr = 0x330;
                else
                    azt2316a->cur_mpu401_addr = 0x300;

                if (val & 0x8)
                    azt2316a->cur_mpu401_enabled = 1;
                else
                    azt2316a->cur_mpu401_enabled = 0;
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
        }
        /* update sbprov2 configs */
        sb_dsp_setaddr(&azt2316a->sb->dsp, azt2316a->cur_addr);
        sb_dsp_setirq(&azt2316a->sb->dsp, azt2316a->cur_irq);
        sb_dsp_setdma8(&azt2316a->sb->dsp, azt2316a->cur_dma);

        mpu401_change_addr(azt2316a->mpu, azt2316a->cur_mpu401_addr);
        mpu401_setirq(azt2316a->mpu, azt2316a->cur_mpu401_irq);
    }
}

static void
azt2316a_config_write(uint16_t addr, uint8_t val, void *p)
{
    azt2316a_t *azt2316a = (azt2316a_t *) p;
    uint8_t     temp;

    if (azt2316a->type == SB_SUBTYPE_CLONE_AZT1605_0X0C) {
        azt1605_config_write(addr, val, azt2316a);
        return;
    }

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
        }
        /* update sbprov2 configs */
        sb_dsp_setaddr(&azt2316a->sb->dsp, azt2316a->cur_addr);
        sb_dsp_setirq(&azt2316a->sb->dsp, azt2316a->cur_irq);
        sb_dsp_setdma8(&azt2316a->sb->dsp, azt2316a->cur_dma);

        mpu401_change_addr(azt2316a->mpu, azt2316a->cur_mpu401_addr);
        mpu401_setirq(azt2316a->mpu, azt2316a->cur_mpu401_irq);
    }
}

/* How it behaves when one or another is activated may affect games auto-detecting (and will also use more of the limited system resources!) */
void
azt2316a_enable_wss(uint8_t enable, void *p)
{
    azt2316a_t *azt2316a = (azt2316a_t *) p;

    if (enable)
        azt2316a->cur_mode = 1;
    else
        azt2316a->cur_mode = 0;
}

static void
azt2316a_get_buffer(int32_t *buffer, int len, void *p)
{
    azt2316a_t *azt2316a = (azt2316a_t *) p;
    int         c;

    /* wss part */
    ad1848_update(&azt2316a->ad1848);
    for (c = 0; c < len * 2; c++)
        buffer[c] += (azt2316a->ad1848.buffer[c] / 2);

    azt2316a->ad1848.pos = 0;

    /* sbprov2 part */
    sb_get_buffer_sbpro(buffer, len, azt2316a->sb);
}

static void *
azt_init(const device_t *info)
{
    FILE       *f;
    char       *fn = NULL;
    int         i;
    int         loaded_from_eeprom = 0;
    uint16_t    addr_setting;
    uint8_t     read_eeprom[AZTECH_EEPROM_SIZE];
    azt2316a_t *azt2316a = malloc(sizeof(azt2316a_t));
    memset(azt2316a, 0, sizeof(azt2316a_t));

    azt2316a->type = info->local & 0x7fffffff;

    if (azt2316a->type == SB_SUBTYPE_CLONE_AZT1605_0X0C) {
        fn = "azt1605.nvr";
    } else if (azt2316a->type == SB_SUBTYPE_CLONE_AZT2316A_0X11) {
        if (info->local & 0x80000000)
            fn = "acermagic_s20.nvr";
        else
            fn = "azt2316a.nvr";
    }

    /* config */
    f = nvr_fopen(fn, "rb");
    if (f) {
        uint8_t checksum = 0x7f;
        uint8_t saved_checksum;
        size_t  res;

        res = fread(read_eeprom, AZTECH_EEPROM_SIZE, 1, f);
        for (i = 0; i < AZTECH_EEPROM_SIZE; i++)
            checksum += read_eeprom[i];

        res = fread(&saved_checksum, sizeof(saved_checksum), 1, f);
        (void) res;

        fclose(f);

        if (checksum == saved_checksum)
            loaded_from_eeprom = 1;
    }

    if (!loaded_from_eeprom) {
        if (azt2316a->type == SB_SUBTYPE_CLONE_AZT2316A_0X11) {
            read_eeprom[0]  = 0x00;
            read_eeprom[1]  = 0x00;
            read_eeprom[2]  = 0x00;
            read_eeprom[3]  = 0x00;
            read_eeprom[4]  = 0x00;
            read_eeprom[5]  = 0x00;
            read_eeprom[6]  = 0x00;
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
            read_eeprom[0]  = 0x80;
            read_eeprom[1]  = 0x80;
            read_eeprom[2]  = 0x9F;
            read_eeprom[3]  = 0x13;
            read_eeprom[4]  = 0x16;
            read_eeprom[5]  = 0x13;
            read_eeprom[6]  = 0x00;
            read_eeprom[7]  = 0x00;
            read_eeprom[8]  = 0x16;
            read_eeprom[9]  = 0x0B;
            read_eeprom[10] = 0x06;
            read_eeprom[11] = 0x01;
            read_eeprom[12] = 0x1C;
            read_eeprom[13] = 0x14;
            read_eeprom[14] = 0x04;
            read_eeprom[15] = 0x1C;
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

        if (info->local & 0x80000000)
            azt2316a->cur_dma = 1;
        else  switch (azt2316a->config_word & (3 << 6)) {
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
        if (info->local & 0x80000000)
            azt2316a->cur_dma     = 1;
        else
            azt2316a->cur_dma     = device_get_config_int("sb_dma8"); // TODO: investigate TSR to make this work with it - there is no software configurable DMA8?
        azt2316a->cur_wss_irq = device_get_config_int("wss_irq");
        azt2316a->cur_wss_dma = device_get_config_int("wss_dma");
        azt2316a->cur_mode    = 0;
    }

    addr_setting = device_get_config_hex16("addr");
    if (addr_setting)
        azt2316a->cur_addr = addr_setting;

    azt2316a->wss_interrupt_after_config = device_get_config_int("wss_interrupt_after_config");

    /* wss part */
    if (info->local & 0x80000000)
        ad1848_init(&azt2316a->ad1848, AD1848_TYPE_CS4231);
    else
        ad1848_init(&azt2316a->ad1848, device_get_config_int("codec"));

    ad1848_setirq(&azt2316a->ad1848, azt2316a->cur_wss_irq);
    ad1848_setdma(&azt2316a->ad1848, azt2316a->cur_wss_dma);

    io_sethandler(azt2316a->cur_addr + 0x0400, 0x0040, azt2316a_config_read, NULL, NULL, azt2316a_config_write, NULL, NULL, azt2316a);
    io_sethandler(azt2316a->cur_wss_addr, 0x0004, azt2316a_wss_read, NULL, NULL, azt2316a_wss_write, NULL, NULL, azt2316a);
    io_sethandler(azt2316a->cur_wss_addr + 0x0004, 0x0004, ad1848_read, NULL, NULL, ad1848_write, NULL, NULL, &azt2316a->ad1848);

    /* sbprov2 part */
    /*sbpro port mappings. 220h or 240h.
      2x0 to 2x3 -> FM chip (18 voices)
      2x4 to 2x5 -> Mixer interface
      2x6, 2xA, 2xC, 2xE -> DSP chip
      2x8, 2x9, 388 and 389 FM chip (9 voices).*/
    azt2316a->sb = malloc(sizeof(sb_t));
    memset(azt2316a->sb, 0, sizeof(sb_t));

    azt2316a->sb->opl_enabled = device_get_config_int("opl");

    for (i = 0; i < AZTECH_EEPROM_SIZE; i++)
        azt2316a->sb->dsp.azt_eeprom[i] = read_eeprom[i];

    if (azt2316a->sb->opl_enabled)
        fm_driver_get(FM_YMF262, &azt2316a->sb->opl);

    sb_dsp_init(&azt2316a->sb->dsp, SBPRO2, azt2316a->type, azt2316a);
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
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, azt2316a->sb);

    if (azt2316a->cur_mpu401_enabled) {
        azt2316a->mpu = (mpu_t *) malloc(sizeof(mpu_t));
        memset(azt2316a->mpu, 0, sizeof(mpu_t));
        mpu401_init(azt2316a->mpu, azt2316a->cur_mpu401_addr, azt2316a->cur_mpu401_irq, M_UART, device_get_config_int("receive_input401"));
    } else
        azt2316a->mpu = NULL;

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &azt2316a->sb->dsp);

    return azt2316a;
}

static void
azt_close(void *p)
{
    azt2316a_t *azt2316a = (azt2316a_t *) p;
    char       *fn       = NULL;
    FILE       *f;
    uint8_t     checksum = 0x7f;
    int         i;

    if (azt2316a->type == SB_SUBTYPE_CLONE_AZT1605_0X0C) {
        fn = "azt1605.nvr";
    } else if (azt2316a->type == SB_SUBTYPE_CLONE_AZT2316A_0X11) {
        fn = "azt2316a.nvr";
    }

    /* always save to eeprom (recover from bad values) */
    f = nvr_fopen(fn, "wb");
    if (f) {
        for (i = 0; i < AZTECH_EEPROM_SIZE; i++)
            checksum += azt2316a->sb->dsp.azt_eeprom[i];
        fwrite(azt2316a->sb->dsp.azt_eeprom, AZTECH_EEPROM_SIZE, 1, f);

        // TODO: confirm any models saving mixer settings to EEPROM and implement reading back
        // TODO: should remember to save wss duplex setting if 86Box has voice recording implemented in the future? Also, default azt2316a->wss_config
        // TODO: azt2316a->cur_mode is not saved to EEPROM?
        fwrite(&checksum, sizeof(checksum), 1, f);

        fclose(f);
    }

    sb_close(azt2316a->sb);

    free(azt2316a);
}

static void
azt_speed_changed(void *p)
{
    azt2316a_t *azt2316a = (azt2316a_t *) p;

    ad1848_speed_changed(&azt2316a->ad1848);
    sb_speed_changed(azt2316a->sb);
}

static const device_config_t azt1605_config[] = {
  // clang-format off
    {
        .name = "codec",
        .description = "CODEC",
        .type = CONFIG_SELECTION,
        .selection = {
            {
                .description = "CS4248",
                .value = AD1848_TYPE_CS4248
            },
            {
                .description = "CS4231",
                .value = AD1848_TYPE_CS4231
            },
        },
        .default_int = AD1848_TYPE_CS4248
    },
    {
        .name = "wss_interrupt_after_config",
        .description = "Raise CODEC interrupt on CODEC setup (needed by some drivers)",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name = "addr",
        .description = "SB Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "0x220",
                .value = 0x220
            },
            {
                .description = "0x240",
                .value = 0x240
            },
            {
                .description = "Use EEPROM setting",
                .value = 0
            },
            {
                .description = ""
            }
        }
    },
    {
        .name = "sb_dma8",
        .description = "SB low DMA",
        .type = CONFIG_SELECTION,
        .selection = {
            {
                .description = "DMA 0",
                .value = 0
            },
            {
                .description = "DMA 1",
                .value = 1
            },
            {
                .description = "DMA 3",
                .value = 3
            },
            {
                .description = ""
            }
        },
        .default_int = 1
    },
    {
        .name = "wss_irq",
        .description = "WSS IRQ",
        .type = CONFIG_SELECTION,
        .selection = {
            {
                .description = "IRQ 11",
                .value = 11
            },
            {
                .description = "IRQ 10",
                .value = 10
            },
            {
                .description = "IRQ 7",
                .value = 7
            },
            {
                .description = ""
            }
        },
        .default_int = 10
    },
        {
            .name = "wss_dma",
            .description = "WSS DMA",
            .type = CONFIG_SELECTION,
            .selection = {
                {
                    .description = "DMA 0",
                    .value = 0
                },
                {
                    .description = "DMA 1",
                    .value = 1
                },
                {
                    .description = "DMA 3",
                    .value = 3
                },
                {
                    .description = ""
                }
            },
            .default_int = 0
        },
    {
        .name = "opl",
        .description = "Enable OPL",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    {
        .name = "receive_input",
        .description = "Receive input (SB MIDI)",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    {
        .name = "receive_input401",
        .description = "Receive input (MPU-401)",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

static const device_config_t azt2316a_config[] = {
  // clang-format off
    {
        .name = "codec",
        .description = "CODEC",
        .type = CONFIG_SELECTION,
        .selection = {
            {
                .description = "CS4248",
                .value = AD1848_TYPE_CS4248
            },
            {
                .description = "CS4231",
                .value = AD1848_TYPE_CS4231
            },
        },
        .default_int = AD1848_TYPE_CS4248
    },
    {
        .name = "wss_interrupt_after_config",
        .description = "Raise CODEC interrupt on CODEC setup (needed by some drivers)",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name = "addr",
        .description = "SB Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "0x220",
                .value = 0x220
            },
            {
                .description = "0x240",
                .value = 0x240
            },
            {
                .description = "Use EEPROM setting",
                .value = 0
            },
            {
                .description = ""
            }
        }
    },
    {
        .name = "wss_irq",
        .description = "WSS IRQ",
        .type = CONFIG_SELECTION,
        .selection = {
            {
                .description = "IRQ 11",
                .value = 11
            },
            {
                .description = "IRQ 10",
                .value = 10
            },
            {
                .description = "IRQ 7",
                .value = 7
            },
            {
                .description = ""
            }
        },
        .default_int = 10
    },
    {
        .name = "wss_dma",
        .description = "WSS DMA",
        .type = CONFIG_SELECTION,
        .selection = {
            {
                .description = "DMA 0",
                .value = 0
            },
            {
                .description = "DMA 1",
                .value = 1
            },
            {
                .description = "DMA 3",
                .value = 3
            },
            {
                .description = ""
            }
        },
        .default_int = 0
    },
    {
        .name = "opl",
        .description = "Enable OPL",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    {
        .name = "receive_input",
        .description = "Receive input (SB MIDI)",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    {
        .name = "receive_input401",
        .description = "Receive input (MPU-401)",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

static const device_config_t acermagic_s20_config[] = {
  // clang-format off
    {
        .name = "wss_interrupt_after_config",
        .description = "Raise CODEC interrupt on CODEC setup (needed by some drivers)",
        .type = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name = "addr",
        .description = "SB Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "0x220",
                .value = 0x220
            },
            {
                .description = "0x240",
                .value = 0x240
            },
            {
                .description = "Use EEPROM setting",
                .value = 0
            },
            {
                .description = ""
            }
        }
    },
    {
        .name = "wss_irq",
        .description = "WSS IRQ",
        .type = CONFIG_SELECTION,
        .selection = {
            {
                .description = "IRQ 11",
                .value = 11
            },
            {
                .description = "IRQ 10",
                .value = 10
            },
            {
                .description = "IRQ 7",
                .value = 7
            },
            {
                .description = ""
            }
        },
        .default_int = 10
    },
    {
        .name = "opl",
        .description = "Enable OPL",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    {
        .name = "receive_input",
        .description = "Receive input (SB MIDI)",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    {
        .name = "receive_input401",
        .description = "Receive input (MPU-401)",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

const device_t azt2316a_device = {
    .name          = "Aztech Sound Galaxy Pro 16 AB (Washington)",
    .internal_name = "azt2316a",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = SB_SUBTYPE_CLONE_AZT2316A_0X11,
    .init          = azt_init,
    .close         = azt_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = azt_speed_changed,
    .force_redraw  = NULL,
    .config        = azt2316a_config
};

const device_t acermagic_s20_device = {
    .name          = "AcerMagic S20",
    .internal_name = "acermagic_s20",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = SB_SUBTYPE_CLONE_AZT2316A_0X11 | 0x80000000,
    .init          = azt_init,
    .close         = azt_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = azt_speed_changed,
    .force_redraw  = NULL,
    .config        = acermagic_s20_config
};

const device_t azt1605_device = {
    .name          = "Aztech Sound Galaxy Nova 16 Extra (Clinton)",
    .internal_name = "azt1605",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = SB_SUBTYPE_CLONE_AZT1605_0X0C,
    .init          = azt_init,
    .close         = azt_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = azt_speed_changed,
    .force_redraw  = NULL,
    .config        = azt1605_config
};
