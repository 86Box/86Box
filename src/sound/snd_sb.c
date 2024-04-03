/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           Sound Blaster emulation.
 *
 *
 *
 * Authors:  Sarah Walker, <https://pcem-emulator.co.uk/>
 *           Miran Grca, <mgrca8@gmail.com>
 *           TheCollector1995, <mariogplayer@gmail.com>
 *
 *           Copyright 2008-2020 Sarah Walker.
 *           Copyright 2016-2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/filters.h>
#include <86box/gameport.h>
#include <86box/hdc.h>
#include <86box/isapnp.h>
#include <86box/hdc_ide.h>
#include <86box/io.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/midi.h>
#include <86box/pic.h>
#include <86box/rom.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/snd_sb.h>
#include <86box/plat_unused.h>

#define PNP_ROM_SB_VIBRA16XV   "roms/sound/creative/CT4170 PnP.BIN"
#define PNP_ROM_SB_VIBRA16C    "roms/sound/creative/CT4180 PnP.BIN"
#define PNP_ROM_SB_32_PNP      "roms/sound/creative/CT3600 PnP.BIN"
#define PNP_ROM_SB_AWE32_PNP   "roms/sound/creative/CT3980 PnP.BIN"
#define PNP_ROM_SB_AWE64_VALUE "roms/sound/creative/CT4520 PnP.BIN"
#define PNP_ROM_SB_AWE64       "roms/sound/creative/CTL009DA.BIN"
#define PNP_ROM_SB_AWE64_GOLD  "roms/sound/creative/CT4540 PnP.BIN"

/* 0 to 7 -> -14dB to 0dB i 2dB steps. 8 to 15 -> 0 to +14dB in 2dB steps.
   Note that for positive dB values, this is not amplitude, it is amplitude - 1. */
static const double sb_bass_treble_4bits[] = {
    0.199526231, 0.25, 0.316227766, 0.398107170, 0.5, 0.63095734, 0.794328234, 1,
    0, 0.25892541, 0.584893192, 1, 1.511886431, 2.16227766, 3, 4.011872336
};

/* Attenuation tables for the mixer. Max volume = 32767 in order to give 6dB of
 * headroom and avoid integer overflow */
// clang-format off
static const double sb_att_2dbstep_5bits[] = {
       25.0,    32.0,    41.0,    51.0,    65.0,    82.0,   103.0,   130.0,   164.0,   206.0,
      260.0,   327.0,   412.0,   519.0,   653.0,   822.0,  1036.0,  1304.0,  1641.0,  2067.0,
     2602.0,  3276.0,  4125.0,  5192.0,  6537.0,  8230.0, 10362.0, 13044.0, 16422.0, 20674.0,
    26027.0, 32767.0
};

static const double sb_att_4dbstep_3bits[] = {
      164.0,  2067.0,  3276.0,  5193.0,  8230.0, 13045.0, 20675.0, 32767.0
};

static const double sb_att_7dbstep_2bits[] = {
      164.0,  6537.0, 14637.0, 32767.0
};

/* Attenuation table for ESS 4-bit microphone volume.
 * The last step is a jump to -48 dB. */
static const double sb_att_1p4dbstep_4bits[] = {
      164.0,  3431.0,  4031.0,  4736.0,  5565.0,  6537.0,  7681.0,  9025.0,
    10603.0, 12458.0, 14637.0, 17196.0, 20204.0, 23738.0, 27889.0, 32767.0
};

/* Attenuation table for ESS 4-bit mixer volume.
 * The last step is a jump to -48 dB. */
static const double sb_att_2dbstep_4bits[] = {
      164.0,  1304.0,  1641.0,  2067.0,  2602.0,  3276.0,  4125.0,  5192.0,
     6537.0,  8230.0, 10362.0, 13044.0, 16422.0, 20674.0, 26027.0, 32767.0
};

/* Attenuation table for ESS 3-bit PC speaker volume. */
static const double sb_att_3dbstep_3bits[] = {
        0.0,  4125.0,  5826.0,  8230.0, 11626.0, 16422.0, 23197.0, 32767.0
};
// clang-format on

static const uint16_t sb_mcv_addr[8]     = { 0x200, 0x210, 0x220, 0x230, 0x240, 0x250, 0x260, 0x270 };
static const int      sb_pro_mcv_irqs[4] = { 7, 5, 3, 3 };

/* Each card in the SB16 family has a million variants, and it shows in the large variety of device IDs for the PnP models.
   This ROM was reconstructed in a best-effort basis around a pnpdump output log found in a forum. */
static uint8_t sb_16_pnp_rom[] = {
    // clang-format off
    0x0e, 0x8c, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, /* CTL0024, dummy checksum (filled in by isapnp_add_card) */
    0x0a, 0x10, 0x10, /* PnP version 1.0, vendor version 1.0 */
    0x82, 0x11, 0x00, 'C', 'r', 'e', 'a', 't', 'i', 'v', 'e', ' ', 'S', 'B', '1', '6', ' ', 'P', 'n', 'P', /* ANSI identifier */

    0x16, 0x0e, 0x8c, 0x00, 0x31, 0x00, 0x65, /* logical device CTL0031, supports vendor-specific registers 0x39/0x3A/0x3D/0x3F */
    0x82, 0x05, 0x00, 'A', 'u', 'd', 'i', 'o', /* ANSI identifier */
    0x31, 0x00, /* start dependent functions, preferred */
        0x22, 0x20, 0x00, /* IRQ 5 */
        0x2a, 0x02, 0x08, /* DMA 1, compatibility, no count by word, count by byte, not bus master, 8-bit only */
        0x2a, 0x20, 0x12, /* DMA 5, compatibility, count by word, no count by byte, not bus master, 16-bit only */
        0x47, 0x01, 0x20, 0x02, 0x20, 0x02, 0x01, 0x10, /* I/O 0x220, decodes 16-bit, 1-byte alignment, 16 addresses */
        0x47, 0x01, 0x30, 0x03, 0x30, 0x03, 0x01, 0x02, /* I/O 0x330, decodes 16-bit, 1-byte alignment, 2 addresses */
        0x47, 0x01, 0x88, 0x03, 0x88, 0x03, 0x01, 0x04, /* I/O 0x388, decodes 16-bit, 1-byte alignment, 4 addresses */
    0x31, 0x01, /* start dependent functions, acceptable */
        0x22, 0xa0, 0x04, /* IRQ 5/7/10 */
        0x2a, 0x0b, 0x08, /* DMA 0/1/3, compatibility, no count by word, count by byte, not bus master, 8-bit only */
        0x2a, 0xe0, 0x12, /* DMA 5/6/7, compatibility, count by word, no count by byte, not bus master, 16-bit only */
        0x47, 0x01, 0x20, 0x02, 0x80, 0x02, 0x20, 0x10, /* I/O 0x220-0x280, decodes 16-bit, 32-byte alignment, 16 addresses */
        0x47, 0x01, 0x00, 0x03, 0x30, 0x03, 0x30, 0x02, /* I/O 0x300-0x330, decodes 16-bit, 48-byte alignment, 2 addresses */
        0x47, 0x01, 0x88, 0x03, 0x88, 0x03, 0x01, 0x04, /* I/O 0x388, decodes 16-bit, 1-byte alignment, 4 addresses */
    0x31, 0x01, /* start dependent functions, acceptable */
        0x22, 0xa0, 0x04, /* IRQ 5/7/10 */
        0x2a, 0x0b, 0x08, /* DMA 0/1/3, compatibility, no count by word, count by byte, not bus master, 8-bit only */
        0x2a, 0xe0, 0x12, /* DMA 5/6/7, compatibility, count by word, no count by byte, not bus master, 16-bit only */
        0x47, 0x01, 0x20, 0x02, 0x80, 0x02, 0x20, 0x10, /* I/O 0x220-0x280, decodes 16-bit, 32-byte alignment, 16 addresses */
        0x47, 0x01, 0x00, 0x03, 0x30, 0x03, 0x30, 0x02, /* I/O 0x300-0x330, decodes 16-bit, 48-byte alignment, 2 addresses */
    0x31, 0x02, /* start dependent functions, functional */
        0x22, 0xa0, 0x04, /* IRQ 5/7/10 */
        0x2a, 0x0b, 0x08, /* DMA 0/1/3, compatibility, no count by word, count by byte, not bus master, 8-bit only */
        0x2a, 0xe0, 0x12, /* DMA 5/6/7, compatibility, count by word, no count by byte, not bus master, 16-bit only */
        0x47, 0x01, 0x20, 0x02, 0x80, 0x02, 0x20, 0x10, /* I/O 0x220-0x280, decodes 16-bit, 32-byte alignment, 16 addresses */
    0x31, 0x02, /* start dependent functions, functional */
        0x22, 0xa0, 0x04, /* IRQ 5/7/10 */
        0x2a, 0x0b, 0x08, /* DMA 0/1/3, compatibility, no count by word, count by byte, not bus master, 8-bit only */
        0x47, 0x01, 0x20, 0x02, 0x80, 0x02, 0x20, 0x10, /* I/O 0x220-0x280, decodes 16-bit, 32-byte alignment, 16 addresses */
        0x47, 0x01, 0x00, 0x03, 0x30, 0x03, 0x30, 0x02, /* I/O 0x300-0x330, decodes 16-bit, 48-byte alignment, 2 addresses */
        0x47, 0x01, 0x88, 0x03, 0x88, 0x03, 0x01, 0x04, /* I/O 0x388, decodes 16-bit, 1-byte alignment, 4 addresses */
    0x31, 0x02, /* start dependent functions, functional */
        0x22, 0xa0, 0x04, /* IRQ 5/7/10 */
        0x2a, 0x0b, 0x08, /* DMA 0/1/3, compatibility, no count by word, count by byte, not bus master, 8-bit only */
        0x47, 0x01, 0x20, 0x02, 0x80, 0x02, 0x20, 0x10, /* I/O 0x220-0x280, decodes 16-bit, 32-byte alignment, 16 addresses */
        0x47, 0x01, 0x00, 0x03, 0x30, 0x03, 0x30, 0x02, /* I/O 0x300-0x330, decodes 16-bit, 48-byte alignment, 2 addresses */
    0x31, 0x02, /* start dependent functions, functional */
        0x22, 0xa0, 0x04, /* IRQ 5/7/10 */
        0x2a, 0x0b, 0x08, /* DMA 0/1/3, compatibility, no count by word, count by byte, not bus master, 8-bit only */
        0x47, 0x01, 0x20, 0x02, 0x80, 0x02, 0x20, 0x10, /* I/O 0x220-0x280, decodes 16-bit, 32-byte alignment, 16 addresses */
    0x38, /* end dependent functions */

    0x16, 0x0e, 0x8c, 0x20, 0x11, 0x00, 0x5a, /* logical device CTL2011, supports vendor-specific registers 0x39/0x3B/0x3C/0x3E */
    0x1c, 0x41, 0xd0, 0x06, 0x00, /* compatible device PNP0600 */
    0x82, 0x03, 0x00, 'I', 'D', 'E', /* ANSI identifier */
    0x31, 0x00, /* start dependent functions, preferred */
        0x22, 0x00, 0x04, /* IRQ 10 */
        0x47, 0x01, 0x68, 0x01, 0x68, 0x01, 0x01, 0x08, /* I/O 0x168, decodes 16-bit, 1-byte alignment, 8 addresses */
        0x47, 0x01, 0x6e, 0x03, 0x6e, 0x03, 0x01, 0x02, /* I/O 0x36E, decodes 16-bit, 1-byte alignment, 2 addresses */
    0x31, 0x01, /* start dependent functions, acceptable */
        0x22, 0x00, 0x08, /* IRQ 11 */
        0x47, 0x01, 0xe8, 0x01, 0xe8, 0x01, 0x01, 0x08, /* I/O 0x1E8, decodes 16-bit, 1-byte alignment, 8 addresses */
        0x47, 0x01, 0xee, 0x03, 0xee, 0x03, 0x01, 0x02, /* I/O 0x3EE, decodes 16-bit, 1-byte alignment, 2 addresses */
    0x31, 0x01, /* start dependent functions, acceptable */
        0x22, 0x00, 0x8c, /* IRQ 10/11/15 */
        0x47, 0x01, 0x00, 0x01, 0xf8, 0x01, 0x08, 0x08, /* I/O 0x100-0x1F8, decodes 16-bit, 8-byte alignment, 8 addresses */
        0x47, 0x01, 0x00, 0x03, 0xfe, 0x03, 0x02, 0x02, /* I/O 0x300-0x3FE, decodes 16-bit, 2-byte alignment, 2 addresses */
    0x31, 0x02, /* start dependent functions, functional */
        0x22, 0x00, 0x80, /* IRQ 15 */
        0x47, 0x01, 0x70, 0x01, 0x70, 0x01, 0x01, 0x08, /* I/O 0x170, decodes 16-bit, 1-byte alignment, 8 addresses */
        0x47, 0x01, 0x76, 0x03, 0x76, 0x03, 0x01, 0x02, /* I/O 0x376, decodes 16-bit, 1-byte alignment, 1 addresses */
    0x38, /* end dependent functions */

    0x16, 0x41, 0xd0, 0xff, 0xff, 0x00, 0xda, /* logical device PNPFFFF, supports vendor-specific registers 0x38/0x39/0x3B/0x3C/0x3E */
    0x82, 0x08, 0x00, 'R', 'e', 's', 'e', 'r', 'v', 'e', 'd', /* ANSI identifier */
    0x47, 0x01, 0x00, 0x01, 0xf8, 0x03, 0x08, 0x01, /* I/O 0x100-0x3F8, decodes 16-bit, 8-byte alignment, 1 address */

    0x15, 0x0e, 0x8c, 0x70, 0x01, 0x00, /* logical device CTL7001 */
    0x1c, 0x41, 0xd0, 0xb0, 0x2f, /* compatible device PNPB02F */
    0x82, 0x04, 0x00, 'G', 'a', 'm', 'e', /* ANSI identifier */
    0x47, 0x01, 0x00, 0x02, 0x00, 0x02, 0x01, 0x08, /* I/O 0x200, decodes 16-bit, 1-byte alignment, 8 addresses */

    0x79, 0x00 /* end tag, dummy checksum (filled in by isapnp_add_card) */
    // clang-format on
};

#ifdef ENABLE_SB_LOG
int sb_do_log = ENABLE_SB_LOG;

static void
sb_log(const char *fmt, ...)
{
    va_list ap;

    if (sb_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sb_log(fmt, ...)
#endif

/* SB 1, 1.5, MCV, and 2 do not have a mixer, so signal is hardwired. */
static void
sb_get_buffer_sb2(int32_t *buffer, int len, void *priv)
{
    sb_t                    *sb    = (sb_t *) priv;
    const sb_ct1335_mixer_t *mixer = &sb->mixer_sb2;
    double                   out_mono = 0.0;
    double                   out_l = 0.0;
    double                   out_r = 0.0;

    sb_dsp_update(&sb->dsp);

    if (sb->cms_enabled)
        cms_update(&sb->cms);

    for (int c = 0; c < len * 2; c += 2) {
        out_mono = 0.0;
        out_l    = 0.0;
        out_r    = 0.0;

        if (sb->cms_enabled) {
            out_l += sb->cms.buffer[c];
            out_r += sb->cms.buffer[c + 1];
        }

        if (sb->cms_enabled && sb->mixer_enabled) {
            out_l *= mixer->fm;
            out_r *= mixer->fm;
        }

        /* TODO: Recording: I assume it has direct mic and line in like SB2.
                 It is unclear from the docs if it has a filter, but it probably does. */
        /* TODO: Recording: Mic and line In with AGC. */
        if (sb->mixer_enabled)
            out_mono = (sb_iir(0, 0, (double) sb->dsp.buffer[c]) * mixer->voice) / 3.9;
        else
            out_mono = (((sb_iir(0, 0, (double) sb->dsp.buffer[c]) / 1.3) * 65536.0) / 3.0) / 65536.0;
        out_l += out_mono;
        out_r += out_mono;

        if (sb->mixer_enabled) {
            out_l *= mixer->master;
            out_r *= mixer->master;
        }

        buffer[c] += (int32_t) out_l;
        buffer[c + 1] += (int32_t) out_r;
    }

    sb->dsp.pos = 0;

    if (sb->cms_enabled)
        sb->cms.pos = 0;
}

static void
sb_get_music_buffer_sb2(int32_t *buffer, int len, void *priv)
{
    sb_t                    *sb    = (sb_t *) priv;
    const sb_ct1335_mixer_t *mixer = &sb->mixer_sb2;
    double                   out_mono = 0.0;
    double                   out_l = 0.0;
    double                   out_r = 0.0;
    const int32_t           *opl_buf = NULL;

    if (!sb->opl_enabled)
        return;

    opl_buf = sb->opl.update(sb->opl.priv);

    for (int c = 0; c < len * 2; c += 2) {
        out_mono = 0.0;
        out_l    = 0.0;
        out_r    = 0.0;

        if (sb->opl_enabled)
            out_mono = ((double) opl_buf[c]) * 0.7171630859375;

        out_l += out_mono;
        out_r += out_mono;

        if (sb->mixer_enabled) {
            out_l *= mixer->fm;
            out_r *= mixer->fm;
        }

        if (sb->mixer_enabled) {
            out_l *= mixer->master;
            out_r *= mixer->master;
        }

        buffer[c] += (int32_t) out_l;
        buffer[c + 1] += (int32_t) out_r;
    }

    sb->opl.reset_buffer(sb->opl.priv);
}

static void
sb2_filter_cd_audio(UNUSED(int channel), double *buffer, void *priv)
{
    const sb_t              *sb    = (sb_t *) priv;
    const sb_ct1335_mixer_t *mixer = &sb->mixer_sb2;
    double                   c;

    if (sb->mixer_enabled) {
        c       = ((sb_iir(2, 0, *buffer) / 1.3) * mixer->cd) / 3.0;
        *buffer = c * mixer->master;
    } else {
        c       = (((sb_iir(2, 0, (*buffer)) / 1.3) * 65536) / 3.0) / 65536.0;
        *buffer = c;
    }
}

void
sb_get_buffer_sbpro(int32_t *buffer, int len, void *priv)
{
    sb_t                    *sb    = (sb_t *) priv;
    const sb_ct1345_mixer_t *mixer = &sb->mixer_sbpro;
    double                   out_l = 0.0;
    double                   out_r = 0.0;

    sb_dsp_update(&sb->dsp);

    for (int c = 0; c < len * 2; c += 2) {
        out_l = 0.0;
        out_r = 0.0;

        /* TODO: Implement the stereo switch on the mixer instead of on the dsp? */
        if (mixer->output_filter) {
            out_l += (sb_iir(0, 0, (double) sb->dsp.buffer[c]) * mixer->voice_l) / 3.9;
            out_r += (sb_iir(0, 1, (double) sb->dsp.buffer[c + 1]) * mixer->voice_r) / 3.9;
        } else {
            out_l += (sb->dsp.buffer[c] * mixer->voice_l) / 3.0;
            out_r += (sb->dsp.buffer[c + 1] * mixer->voice_r) / 3.0;
        }

        /* TODO: recording CD, Mic with AGC or line in. Note: mic volume does not affect recording. */
        out_l *= mixer->master_l;
        out_r *= mixer->master_r;

        buffer[c] += (int32_t) out_l;
        buffer[c + 1] += (int32_t) out_r;
    }

    sb->dsp.pos = 0;
}

void
sb_get_music_buffer_sbpro(int32_t *buffer, int len, void *priv)
{
    sb_t                    *sb    = (sb_t *) priv;
    const sb_ct1345_mixer_t *mixer = &sb->mixer_sbpro;
    double                   out_l = 0.0;
    double                   out_r = 0.0;
    const int32_t           *opl_buf = NULL;
    const int32_t           *opl2_buf = NULL;

    if (!sb->opl_enabled)
        return;

    if (sb->dsp.sb_type == SBPRO) {
        opl_buf  = sb->opl.update(sb->opl.priv);
        opl2_buf = sb->opl2.update(sb->opl2.priv);
    } else
        opl_buf = sb->opl.update(sb->opl.priv);

    sb_dsp_update(&sb->dsp);

    for (int c = 0; c < len * 2; c += 2) {
        out_l = 0.0;
        out_r = 0.0;

        if (sb->dsp.sb_type == SBPRO) {
            /* Two chips for LEFT and RIGHT channels.
               Each chip stores data into the LEFT channel only (no sample alternating.) */
            out_l = (((double) opl_buf[c]) * mixer->fm_l) * 0.7171630859375;
            out_r = (((double) opl2_buf[c]) * mixer->fm_r) * 0.7171630859375;
        } else {
            out_l = (((double) opl_buf[c]) * mixer->fm_l) * 0.7171630859375;
            out_r = (((double) opl_buf[c + 1]) * mixer->fm_r) * 0.7171630859375;
            if (sb->opl_mix && sb->opl_mixer)
                sb->opl_mix(sb->opl_mixer, &out_l, &out_r);
        }

        /* TODO: recording CD, Mic with AGC or line in. Note: mic volume does not affect recording. */
        out_l *= mixer->master_l;
        out_r *= mixer->master_r;

        buffer[c] += (int32_t) out_l;
        buffer[c + 1] += (int32_t) out_r;
    }

    sb->opl.reset_buffer(sb->opl.priv);
    if (sb->dsp.sb_type == SBPRO)
        sb->opl2.reset_buffer(sb->opl2.priv);
}

void
sbpro_filter_cd_audio(int channel, double *buffer, void *priv)
{
    const sb_t              *sb    = (sb_t *) priv;
    const sb_ct1345_mixer_t *mixer = &sb->mixer_sbpro;
    double                   c;
    double                   cd     = channel ? mixer->cd_r : mixer->cd_l;
    double                   master = channel ? mixer->master_r : mixer->master_l;

    c = (*buffer * cd) / 3.0;
    *buffer = c * master;
}

static void
sb_get_buffer_sb16_awe32(int32_t *buffer, int len, void *priv)
{
    sb_t                    *sb    = (sb_t *) priv;
    const sb_ct1745_mixer_t *mixer = &sb->mixer_sb16;
    int                      c_emu8k = 0;
    double                   out_l = 0.0;
    double                   out_r = 0.0;
    double                   bass_treble;

    sb_dsp_update(&sb->dsp);

    if (sb->dsp.sb_type > SB16)
        emu8k_update(&sb->emu8k);

    for (int c = 0; c < len * 2; c += 2) {
        out_l = 0.0;
        out_r = 0.0;

        if (sb->dsp.sb_type > SB16)
            c_emu8k = ((((c / 2) * FREQ_44100) / SOUND_FREQ) * 2);

        if (sb->dsp.sb_type > SB16) {
            out_l += (((double) sb->emu8k.buffer[c_emu8k]) * mixer->fm_l);
            out_r += (((double) sb->emu8k.buffer[c_emu8k + 1]) * mixer->fm_r);
        }

        if (mixer->output_filter) {
            /* We divide by 3 to get the volume down to normal. */
            out_l += (low_fir_sb16(0, 0, (double) sb->dsp.buffer[c]) * mixer->voice_l) / 3.0;
            out_r += (low_fir_sb16(0, 1, (double) sb->dsp.buffer[c + 1]) * mixer->voice_r) / 3.0;
        } else {
            out_l += (((double) sb->dsp.buffer[c]) * mixer->voice_l) / 3.0;
            out_r += (((double) sb->dsp.buffer[c + 1]) * mixer->voice_r) / 3.0;
        }

        out_l *= mixer->master_l;
        out_r *= mixer->master_r;

        /* This is not exactly how one does bass/treble controls, but the end result is like it.
           A better implementation would reduce the CPU usage. */
        if (mixer->bass_l != 8) {
            bass_treble = sb_bass_treble_4bits[mixer->bass_l];

            if (mixer->bass_l > 8)
                out_l += (low_iir(0, 0, out_l) * bass_treble);
            else if (mixer->bass_l < 8)
                out_l = (out_l *bass_treble + low_cut_iir(0, 0, out_l) * (1.0 - bass_treble));
        }

        if (mixer->bass_r != 8) {
            bass_treble = sb_bass_treble_4bits[mixer->bass_r];

            if (mixer->bass_r > 8)
                out_r += (low_iir(0, 1, out_r) * bass_treble);
            else if (mixer->bass_r < 8)
                out_r = (out_r *bass_treble + low_cut_iir(0, 1, out_r) * (1.0 - bass_treble));
        }

        if (mixer->treble_l != 8) {
            bass_treble = sb_bass_treble_4bits[mixer->treble_l];

            if (mixer->treble_l > 8)
                out_l += (high_iir(0, 0, out_l) * bass_treble);
            else if (mixer->treble_l < 8)
                out_l = (out_l *bass_treble + high_cut_iir(0, 0, out_l) * (1.0 - bass_treble));
        }

        if (mixer->treble_r != 8) {
            bass_treble = sb_bass_treble_4bits[mixer->treble_r];

            if (mixer->treble_r > 8)
                out_r += (high_iir(0, 1, out_r) * bass_treble);
            else if (mixer->treble_r < 8)
                out_r = (out_l *bass_treble + high_cut_iir(0, 1, out_r) * (1.0 - bass_treble));
        }

        buffer[c] += (int32_t) (out_l * mixer->output_gain_L);
        buffer[c + 1] += (int32_t) (out_r * mixer->output_gain_R);
    }

    sb->dsp.pos = 0;

    if (sb->dsp.sb_type > SB16)
        sb->emu8k.pos = 0;
}

static void
sb_get_music_buffer_sb16_awe32(int32_t *buffer, int len, void *priv)
{
    sb_t                    *sb    = (sb_t *) priv;
    const sb_ct1745_mixer_t *mixer = &sb->mixer_sb16;
    int                      dsp_rec_pos = sb->dsp.record_pos_write;
    int                      c_record;
    int32_t                  in_l;
    int32_t                  in_r;
    double                   out_l = 0.0;
    double                   out_r = 0.0;
    double                   bass_treble;
    const int32_t           *opl_buf = NULL;

    if (sb->opl_enabled)
        opl_buf = sb->opl.update(sb->opl.priv);

    for (int c = 0; c < len * 2; c += 2) {
        out_l = 0.0;
        out_r = 0.0;

        if (sb->opl_enabled) {
            out_l = ((double) opl_buf[c]) * mixer->fm_l * 0.7171630859375;
            out_r = ((double) opl_buf[c + 1]) * mixer->fm_r * 0.7171630859375;
        }

        /* TODO: Multi-recording mic with agc/+20db, CD, and line in with channel inversion */
        in_l = (mixer->input_selector_left & INPUT_MIDI_L) ? ((int32_t) out_l) : 0 + (mixer->input_selector_left & INPUT_MIDI_R) ? ((int32_t) out_r)
                                                                                                                                 : 0;
        in_r = (mixer->input_selector_right & INPUT_MIDI_L) ? ((int32_t) out_l) : 0 + (mixer->input_selector_right & INPUT_MIDI_R) ? ((int32_t) out_r)
                                                                                                                                   : 0;

        out_l *= mixer->master_l;
        out_r *= mixer->master_r;

        /* This is not exactly how one does bass/treble controls, but the end result is like it.
           A better implementation would reduce the CPU usage. */
        if (mixer->bass_l != 8) {
            bass_treble = sb_bass_treble_4bits[mixer->bass_l];

            if (mixer->bass_l > 8)
                out_l += (low_iir(1, 0, out_l) * bass_treble);
            else if (mixer->bass_l < 8)
                out_l = (out_l *bass_treble + low_cut_iir(1, 0, out_l) * (1.0 - bass_treble));
        }

        if (mixer->bass_r != 8) {
            bass_treble = sb_bass_treble_4bits[mixer->bass_r];

            if (mixer->bass_r > 8)
                out_r += (low_iir(1, 1, out_r) * bass_treble);
            else if (mixer->bass_r < 8)
                out_r = (out_r *bass_treble + low_cut_iir(1, 1, out_r) * (1.0 - bass_treble));
        }

        if (mixer->treble_l != 8) {
            bass_treble = sb_bass_treble_4bits[mixer->treble_l];

            if (mixer->treble_l > 8)
                out_l += (high_iir(1, 0, out_l) * bass_treble);
            else if (mixer->treble_l < 8)
                out_l = (out_l *bass_treble + high_cut_iir(1, 0, out_l) * (1.0 - bass_treble));
        }

        if (mixer->treble_r != 8) {
            bass_treble = sb_bass_treble_4bits[mixer->treble_r];

            if (mixer->treble_r > 8)
                out_r += (high_iir(1, 1, out_r) * bass_treble);
            else if (mixer->treble_r < 8)
                out_r = (out_l *bass_treble + high_cut_iir(1, 1, out_r) * (1.0 - bass_treble));
        }

        if (sb->dsp.sb_enable_i) {
            c_record = dsp_rec_pos + ((c * sb->dsp.sb_freq) / MUSIC_FREQ);
            in_l <<= mixer->input_gain_L;
            in_r <<= mixer->input_gain_R;

            /* Clip signal */
            if (in_l < -32768)
                in_l = -32768;
            else if (in_l > 32767)
                in_l = 32767;

            if (in_r < -32768)
                in_r = -32768;
            else if (in_r > 32767)
                in_r = 32767;

            sb->dsp.record_buffer[c_record & 0xffff]       = in_l;
            sb->dsp.record_buffer[(c_record + 1) & 0xffff] = in_r;
        }

        buffer[c] += (int32_t) (out_l * mixer->output_gain_L);
        buffer[c + 1] += (int32_t) (out_r * mixer->output_gain_R);
    }

    sb->dsp.record_pos_write += ((len * sb->dsp.sb_freq) / 24000);
    sb->dsp.record_pos_write &= 0xffff;

    if (sb->opl_enabled)
        sb->opl.reset_buffer(sb->opl.priv);
}

void
sb16_awe32_filter_cd_audio(int channel, double *buffer, void *priv)
{
    const sb_t              *sb    = (sb_t *) priv;
    const sb_ct1745_mixer_t *mixer = &sb->mixer_sb16;
    double                   c;
    double                   cd     = channel ? mixer->cd_r : mixer->cd_l /* / 3.0 */;
    double                   master = channel ? mixer->master_r : mixer->master_l;
    int32_t                  bass   = channel ? mixer->bass_r : mixer->bass_l;
    int32_t                  treble = channel ? mixer->treble_r : mixer->treble_l;
    double                   bass_treble;
    double                   output_gain = (channel ? mixer->output_gain_R : mixer->output_gain_L);

    c = ((*buffer) * cd) / 3.0;
    c *= master;

    /* This is not exactly how one does bass/treble controls, but the end result is like it.
       A better implementation would reduce the CPU usage. */
    if (bass != 8) {
        bass_treble = sb_bass_treble_4bits[bass];

        if (bass > 8)
            c += (low_iir(2, channel, c) * bass_treble);
        else if (bass < 8)
            c = (c * bass_treble + low_cut_iir(2, channel, c) * (1.0 - bass_treble));
    }

    if (treble != 8) {
        bass_treble = sb_bass_treble_4bits[treble];

        if (treble > 8)
            c += (high_iir(2, channel, c) * bass_treble);
        else if (treble < 8)
            c = (c * bass_treble + high_cut_iir(2, channel, c) * (1.0 - bass_treble));
    }

    *buffer = c * output_gain;
}

void
sb16_awe32_filter_pc_speaker(int channel, double *buffer, void *priv)
{
    const sb_t              *sb    = (sb_t *) priv;
    const sb_ct1745_mixer_t *mixer = &sb->mixer_sb16;
    double                   c;
    double                   spk    = mixer->speaker;
    double                   master = channel ? mixer->master_r : mixer->master_l;
    int32_t                  bass   = channel ? mixer->bass_r : mixer->bass_l;
    int32_t                  treble = channel ? mixer->treble_r : mixer->treble_l;
    double                   bass_treble;
    double                   output_gain = (channel ? mixer->output_gain_R : mixer->output_gain_L);

    if (mixer->output_filter)
        c = (low_fir_sb16(3, channel, *buffer) * spk) / 3.0;
    else
        c = ((*buffer) * spk) / 3.0;
    c *= master;

    /* This is not exactly how one does bass/treble controls, but the end result is like it.
       A better implementation would reduce the CPU usage. */
    if (bass != 8) {
        bass_treble = sb_bass_treble_4bits[bass];

        if (bass > 8)
            c += (low_iir(3, channel, c) * bass_treble);
        else if (bass < 8)
            c = (c * bass_treble + low_cut_iir(3, channel, c) * (1.0 - bass_treble));
    }

    if (treble != 8) {
        bass_treble = sb_bass_treble_4bits[treble];

        if (treble > 8)
            c += (high_iir(3, channel, c) * bass_treble);
        else if (treble < 8)
            c = (c * bass_treble + high_cut_iir(3, channel, c) * (1.0 - bass_treble));
    }

    *buffer = c * output_gain;
}

void
sb_get_buffer_ess(int32_t *buffer, int len, void *priv)
{
    sb_t              *ess   = (sb_t *) priv;
    const ess_mixer_t *mixer = &ess->mixer_ess;
    double             out_l = 0.0;
    double             out_r = 0.0;

    sb_dsp_update(&ess->dsp);

    for (int c = 0; c < len * 2; c += 2) {
        out_l = 0.0;
        out_r = 0.0;

        /* TODO: Implement the stereo switch on the mixer instead of on the dsp? */
        if (mixer->output_filter) {
            out_l += (low_fir_sb16(0, 0, (double) ess->dsp.buffer[c]) * mixer->voice_l) / 3.0;
            out_r += (low_fir_sb16(0, 1, (double) ess->dsp.buffer[c + 1]) * mixer->voice_r) / 3.0;
        } else {
            out_l += (ess->dsp.buffer[c] * mixer->voice_l) / 3.0;
            out_r += (ess->dsp.buffer[c + 1] * mixer->voice_r) / 3.0;
        }

        /* TODO: recording from the mixer. */
        out_l *= mixer->master_l;
        out_r *= mixer->master_r;

        buffer[c] += (int32_t) out_l;
        buffer[c + 1] += (int32_t) out_r;
    }

    ess->dsp.pos = 0;
}

void
sb_get_music_buffer_ess(int32_t *buffer, int len, void *priv)
{
    sb_t              *ess     = (sb_t *) priv;
    const ess_mixer_t *mixer   = &ess->mixer_ess;
    double             out_l   = 0.0;
    double             out_r   = 0.0;
    const int32_t     *opl_buf = NULL;

    opl_buf = ess->opl.update(ess->opl.priv);

    for (int c = 0; c < len * 2; c += 2) {
        out_l = 0.0;
        out_r = 0.0;

        out_l = (((double) opl_buf[c]) * mixer->fm_l) * 0.7171630859375;
        out_r = (((double) opl_buf[c + 1]) * mixer->fm_r) * 0.7171630859375;
        if (ess->opl_mix && ess->opl_mixer)
            ess->opl_mix(ess->opl_mixer, &out_l, &out_r);

        /* TODO: recording from the mixer. */
        out_l *= mixer->master_l;
        out_r *= mixer->master_r;

        buffer[c] += (int32_t) out_l;
        buffer[c + 1] += (int32_t) out_r;
    }

    ess->opl.reset_buffer(ess->opl.priv);
}

void
ess_filter_cd_audio(int channel, double *buffer, void *priv)
{
    const sb_t        *ess   = (sb_t *) priv;
    const ess_mixer_t *mixer = &ess->mixer_ess;
    double             c;
    double             cd     = channel ? mixer->cd_r : mixer->cd_l;
    double             master = channel ? mixer->master_r : mixer->master_l;

    /* TODO: recording from the mixer. */
    c       = (*buffer * cd) / 3.0;
    *buffer = c * master;
}

void
sb_ct1335_mixer_write(uint16_t addr, uint8_t val, void *priv)
{
    sb_t              *sb    = (sb_t *) priv;
    sb_ct1335_mixer_t *mixer = &sb->mixer_sb2;

    if (!(addr & 1)) {
        mixer->index      = val;
        mixer->regs[0x01] = val;
    } else {
        if (mixer->index == 0) {
            /* Reset */
            mixer->regs[0x02] = mixer->regs[0x06] = 0x08;
            mixer->regs[0x08]                     = 0x00;
            /* Changed default from -46dB to 0dB*/
            mixer->regs[0x0a] = 0x06;
        } else {
            mixer->regs[mixer->index] = val;
            switch (mixer->index) {
                case 0x00:
                case 0x02:
                case 0x06:
                case 0x08:
                case 0x0a:
                    break;

                default:
                    sb_log("sb_ct1335: Unknown register WRITE: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
                    break;
            }
        }

        mixer->master = sb_att_4dbstep_3bits[(mixer->regs[0x02] >> 1) & 0x7] / 32768.0;
        mixer->fm     = sb_att_4dbstep_3bits[(mixer->regs[0x06] >> 1) & 0x7] / 32768.0;
        mixer->cd     = sb_att_4dbstep_3bits[(mixer->regs[0x08] >> 1) & 0x7] / 32768.0;
        mixer->voice  = sb_att_7dbstep_2bits[(mixer->regs[0x0a] >> 1) & 0x3] / 32768.0;
    }
}

uint8_t
sb_ct1335_mixer_read(uint16_t addr, void *priv)
{
    const sb_t              *sb    = (sb_t *) priv;
    const sb_ct1335_mixer_t *mixer = &sb->mixer_sb2;

    if (!(addr & 1))
        return mixer->index;

    switch (mixer->index) {
        case 0x00:
        case 0x02:
        case 0x06:
        case 0x08:
        case 0x0A:
            return mixer->regs[mixer->index];
        default:
            sb_log("sb_ct1335: Unknown register READ: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
            break;
    }

    return 0xff;
}

void
sb_ct1335_mixer_reset(sb_t *sb)
{
    sb_ct1335_mixer_write(0x254, 0, sb);
    sb_ct1335_mixer_write(0x255, 0, sb);
}

void
sb_ct1345_mixer_write(uint16_t addr, uint8_t val, void *priv)
{
    sb_t              *sb    = (sb_t *) priv;
    sb_ct1345_mixer_t *mixer = &sb->mixer_sbpro;

    if (!(addr & 1)) {
        mixer->index      = val;
        mixer->regs[0x01] = val;
    } else {
        if (mixer->index == 0) {
            /* Reset */
            mixer->regs[0x0a] = mixer->regs[0x0c] = 0x00;
            mixer->regs[0x0e]                     = 0x00;
            /* Changed default from -11dB to 0dB */
            mixer->regs[0x04] = mixer->regs[0x22] = 0xee;
            mixer->regs[0x26] = mixer->regs[0x28] = 0xee;
            mixer->regs[0x2e]                     = 0x00;
            sb_dsp_set_stereo(&sb->dsp, mixer->regs[0x0e] & 2);
        } else {
            mixer->regs[mixer->index] = val;

            switch (mixer->index) {
                /* Compatibility: chain registers 0x02 and 0x22 as well as 0x06 and 0x26 */
                case 0x02:
                case 0x06:
                case 0x08:
                    mixer->regs[mixer->index + 0x20] = ((val & 0xe) << 4) | (val & 0xe);
                    break;

                case 0x22:
                case 0x26:
                case 0x28:
                    mixer->regs[mixer->index - 0x20] = (val & 0xe);
                    break;

                /* More compatibility:
                   SoundBlaster Pro selects register 020h for 030h, 022h for 032h,
                   026h for 036h, and 028h for 038h. */
                case 0x30:
                case 0x32:
                case 0x36:
                case 0x38:
                    mixer->regs[mixer->index - 0x10] = (val & 0xee);
                    break;

                case 0x00:
                case 0x04:
                case 0x0a:
                case 0x0c:
                case 0x0e:
                case 0x2e:
                    break;

                default:
                    sb_log("sb_ct1345: Unknown register WRITE: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
                    break;
            }
        }

        mixer->voice_l  = sb_att_4dbstep_3bits[(mixer->regs[0x04] >> 5) & 0x7] / 32768.0;
        mixer->voice_r  = sb_att_4dbstep_3bits[(mixer->regs[0x04] >> 1) & 0x7] / 32768.0;
        mixer->master_l = sb_att_4dbstep_3bits[(mixer->regs[0x22] >> 5) & 0x7] / 32768.0;
        mixer->master_r = sb_att_4dbstep_3bits[(mixer->regs[0x22] >> 1) & 0x7] / 32768.0;
        mixer->fm_l     = sb_att_4dbstep_3bits[(mixer->regs[0x26] >> 5) & 0x7] / 32768.0;
        mixer->fm_r     = sb_att_4dbstep_3bits[(mixer->regs[0x26] >> 1) & 0x7] / 32768.0;
        mixer->cd_l     = sb_att_4dbstep_3bits[(mixer->regs[0x28] >> 5) & 0x7] / 32768.0;
        mixer->cd_r     = sb_att_4dbstep_3bits[(mixer->regs[0x28] >> 1) & 0x7] / 32768.0;
        mixer->line_l   = sb_att_4dbstep_3bits[(mixer->regs[0x2e] >> 5) & 0x7] / 32768.0;
        mixer->line_r   = sb_att_4dbstep_3bits[(mixer->regs[0x2e] >> 1) & 0x7] / 32768.0;

        mixer->mic = sb_att_7dbstep_2bits[(mixer->regs[0x0a] >> 1) & 0x3] / 32768.0;

        mixer->output_filter  = !(mixer->regs[0xe] & 0x20);
        mixer->input_filter   = !(mixer->regs[0xc] & 0x20);
        mixer->in_filter_freq = ((mixer->regs[0xc] & 0x8) == 0) ? 3200 : 8800;
        mixer->stereo         = mixer->regs[0xe] & 2;
        if (mixer->index == 0xe)
            sb_dsp_set_stereo(&sb->dsp, val & 2);

        switch (mixer->regs[0xc] & 6) {
            case 2:
                mixer->input_selector = INPUT_CD_L | INPUT_CD_R;
                break;
            case 6:
                mixer->input_selector = INPUT_LINE_L | INPUT_LINE_R;
                break;
            default:
                mixer->input_selector = INPUT_MIC;
                break;
        }

        /* TODO: pcspeaker volume? Or is it not worth? */
    }
}

uint8_t
sb_ct1345_mixer_read(uint16_t addr, void *priv)
{
    const sb_t              *sb    = (sb_t *) priv;
    const sb_ct1345_mixer_t *mixer = &sb->mixer_sbpro;

    if (!(addr & 1))
        return mixer->index;

    switch (mixer->index) {
        case 0x00:
        case 0x04:
        case 0x0a:
        case 0x0c:
        case 0x0e:
        case 0x22:
        case 0x26:
        case 0x28:
        case 0x2e:
        case 0x02:
        case 0x06:
        case 0x30:
        case 0x32:
        case 0x36:
        case 0x38:
            return mixer->regs[mixer->index];

        default:
            sb_log("sb_ct1345: Unknown register READ: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
            break;
    }

    return 0xff;
}

void
sb_ct1345_mixer_reset(sb_t *sb)
{
    sb_ct1345_mixer_write(4, 0, sb);
    sb_ct1345_mixer_write(5, 0, sb);
}

void
sb_ct1745_mixer_write(uint16_t addr, uint8_t val, void *priv)
{
    sb_t              *sb    = (sb_t *) priv;
    sb_ct1745_mixer_t *mixer = &sb->mixer_sb16;

    if (!(addr & 1))
        mixer->index = val;
    else {
        /* DESCRIPTION:
           Contains previously selected register value.  Mixer Data Register value.
           NOTES:
           SoundBlaster 16 sets bit 7 if previous mixer index invalid.
           Status bytes initially 080h on startup for all but level bytes (SB16). */

        sb_log("CT1745: [W] %02X = %02X\n", mixer->index, val);

        if (mixer->index == 0) {
            /* Reset: Changed defaults from -14dB to 0dB */

            mixer->regs[0x30] = mixer->regs[0x31] = 0xf8;
            mixer->regs[0x32] = mixer->regs[0x33] = 0xf8;
            mixer->regs[0x34] = mixer->regs[0x35] = 0xf8;
            mixer->regs[0x36] = mixer->regs[0x37] = 0xf8;
            mixer->regs[0x38] = mixer->regs[0x39] = 0x00;

            mixer->regs[0x3a] = 0x00;
            /* Speaker control - it appears to be in steps of 64. */
            mixer->regs[0x3b] = 0x80;

            mixer->regs[0x3c] = (OUTPUT_MIC | OUTPUT_CD_R | OUTPUT_CD_L | OUTPUT_LINE_R | OUTPUT_LINE_L);
            mixer->regs[0x3d] = (INPUT_MIC | INPUT_CD_L | INPUT_LINE_L | INPUT_MIDI_L);
            mixer->regs[0x3e] = (INPUT_MIC | INPUT_CD_R | INPUT_LINE_R | INPUT_MIDI_R);

            mixer->regs[0x3f] = mixer->regs[0x40] = 0x00;
            mixer->regs[0x41] = mixer->regs[0x42] = 0x00;

            mixer->regs[0x44] = mixer->regs[0x45] = 0x80;
            mixer->regs[0x46] = mixer->regs[0x47] = 0x80;

            /* 0x43 = Mic AGC (Automatic Gain Control?) according to Linux's sb.h.
                      NSC LM4560 datasheet: Bit 0: 1 = Enable, 0 = Disable;
                      Another source says this: Bit 0: 0 = AGC on (default), 1 = Fixed gain of 20 dB. */
            mixer->regs[0x43] = 0x00;

            mixer->regs[0x49] = mixer->regs[0x4a] = 0x80;

            mixer->regs[0x83]  = 0xff;
            sb->dsp.sb_irqm8   = 0;
            sb->dsp.sb_irqm16  = 0;
            sb->dsp.sb_irqm401 = 0;

            mixer->regs[0xfd] = 0x10;
            mixer->regs[0xfe] = 0x06;

            mixer->regs[0xff] = sb->dsp.sb_16_dma_supported ? 0x05 : 0x03;

            sb_dsp_setdma16_enabled(&sb->dsp, 0x01);
            sb_dsp_setdma16_translate(&sb->dsp, mixer->regs[0xff] & 0x02);
        } else
            mixer->regs[mixer->index] = val;

        switch (mixer->index) {
            /* SB1/2 compatibility? */
            case 0x02:
                mixer->regs[0x30] = ((mixer->regs[0x02] & 0xf) << 4) | 0x8;
                mixer->regs[0x31] = ((mixer->regs[0x02] & 0xf) << 4) | 0x8;
                break;
            case 0x06:
                mixer->regs[0x34] = ((mixer->regs[0x06] & 0xf) << 4) | 0x8;
                mixer->regs[0x35] = ((mixer->regs[0x06] & 0xf) << 4) | 0x8;
                break;
            case 0x08:
                mixer->regs[0x36] = ((mixer->regs[0x08] & 0xf) << 4) | 0x8;
                mixer->regs[0x37] = ((mixer->regs[0x08] & 0xf) << 4) | 0x8;
                break;
            /* SBPro compatibility. Copy values to sb16 registers. */
            case 0x22:
                mixer->regs[0x30] = (mixer->regs[0x22] & 0xf0) | 0x8;
                mixer->regs[0x31] = ((mixer->regs[0x22] & 0xf) << 4) | 0x8;
                break;
            case 0x04:
                mixer->regs[0x32] = (mixer->regs[0x04] & 0xf0) | 0x8;
                mixer->regs[0x33] = ((mixer->regs[0x04] & 0xf) << 4) | 0x8;
                break;
            case 0x26:
                mixer->regs[0x34] = (mixer->regs[0x26] & 0xf0) | 0x8;
                mixer->regs[0x35] = ((mixer->regs[0x26] & 0xf) << 4) | 0x8;
                break;
            case 0x28:
                mixer->regs[0x36] = (mixer->regs[0x28] & 0xf0) | 0x8;
                mixer->regs[0x37] = ((mixer->regs[0x28] & 0xf) << 4) | 0x8;
                break;
            case 0x0A:
                mixer->regs[0x3a] = (mixer->regs[0x0a] << 5) | 0x18;
                break;
            case 0x2e:
                mixer->regs[0x38] = (mixer->regs[0x2e] & 0xf0) | 0x8;
                mixer->regs[0x39] = ((mixer->regs[0x2e] & 0xf) << 4) | 0x8;
                break;

            /* (DSP 4.xx feature):
               The Interrupt Setup register, addressed as register 80h on the Mixer register map,
               is used to configure or determine the Interrupt request line.
               The DMA setup register, addressed as register 81h on the Mixer register map, is
               used to configure or determine the DMA channels.

               Note: Registers 80h and 81h are Read-only for PnP boards. */
            case 0x80:
                if (!sb->pnp) {
                    if (val & 0x01)
                        sb_dsp_setirq(&sb->dsp, 2);
                    if (val & 0x02)
                        sb_dsp_setirq(&sb->dsp, 5);
                    if (val & 0x04)
                        sb_dsp_setirq(&sb->dsp, 7);
                    if (val & 0x08)
                        sb_dsp_setirq(&sb->dsp, 10);
                }
                break;

            case 0x81:
                /* The documentation is confusing. sounds as if multple dma8 channels could
                   be set. */
                if (!sb->pnp) {
                    if (val & 0x01)
                        sb_dsp_setdma8(&sb->dsp, 0);
                    else if (val & 0x02)
                        sb_dsp_setdma8(&sb->dsp, 1);
                    else if (val & 0x08)
                        sb_dsp_setdma8(&sb->dsp, 3);

                    sb_dsp_setdma16(&sb->dsp, 4);
                    if (val & 0x20)
                        sb_dsp_setdma16(&sb->dsp, 5);
                    else if (val & 0x40)
                        sb_dsp_setdma16(&sb->dsp, 6);
                    else if (val & 0x80)
                        sb_dsp_setdma16(&sb->dsp, 7);
                }
                break;

            case 0x83:
                /* Interrupt mask. */
                sb_update_mask(&sb->dsp, !(val & 0x01), !(val & 0x02), !(val & 0x04));
                break;

            case 0x84:
                /* MPU Control register, per the Linux source code. */
                /* Bits 2-1: MPU-401 address:
                       0, 0 = 330h;
                       0, 1 = Disabled;
                       1, 0 = 300h;
                       1, 1 = ???? (Reserved?)
                   Bit 0: Gameport address:
                       0, 0 = 200-207h;
                       0, 1 = Disabled
                 */
                if (!sb->pnp) {
                    if (sb->mpu != NULL) {
                        if ((val & 0x06) == 0x00)
                            mpu401_change_addr(sb->mpu, 0x330);
                        else if ((val & 0x06) == 0x04)
                            mpu401_change_addr(sb->mpu, 0x300);
                        else if ((val & 0x06) == 0x02)
                            mpu401_change_addr(sb->mpu, 0);
                    }
                    sb->gameport_addr = 0;
                    gameport_remap(sb->gameport, 0);
                    if (!(val & 0x01)) {
                        sb->gameport_addr = 0x200;
                        gameport_remap(sb->gameport, 0x200);
                    }
                }
                break;

            case 0xff:
                if ((sb->dsp.sb_type > SBAWE32) && !sb->dsp.sb_16_dma_supported) {
                    /*
                       Bit 5: High DMA channel enabled (0 = yes, 1 = no);
                       Bit 2: ????;
                       Bit 1: ???? (16-bit to 8-bit translation?);
                       Bit 0: ????
                       Seen values: 20, 05, 04, 03
                     */
                    sb_dsp_setdma16_enabled(&sb->dsp, !(val & 0x20));
#ifdef TOGGLABLE_TRANSLATION
                    sb_dsp_setdma16_translate(&sb->dsp, val & 0x02);
#endif
                }
                break;

            default:
                break;
        }

        mixer->output_selector      = mixer->regs[0x3c];
        mixer->input_selector_left  = mixer->regs[0x3d];
        mixer->input_selector_right = mixer->regs[0x3e];

        mixer->master_l = sb_att_2dbstep_5bits[mixer->regs[0x30] >> 3] / 32768.0;
        mixer->master_r = sb_att_2dbstep_5bits[mixer->regs[0x31] >> 3] / 32768.0;
        mixer->voice_l  = sb_att_2dbstep_5bits[mixer->regs[0x32] >> 3] / 32768.0;
        mixer->voice_r  = sb_att_2dbstep_5bits[mixer->regs[0x33] >> 3] / 32768.0;
        mixer->fm_l     = sb_att_2dbstep_5bits[mixer->regs[0x34] >> 3] / 32768.0;
        mixer->fm_r     = sb_att_2dbstep_5bits[mixer->regs[0x35] >> 3] / 32768.0;
        mixer->cd_l     = (mixer->output_selector & OUTPUT_CD_L) ? (sb_att_2dbstep_5bits[mixer->regs[0x36] >> 3] / 32768.0) : 0.0;
        mixer->cd_r     = (mixer->output_selector & OUTPUT_CD_R) ? (sb_att_2dbstep_5bits[mixer->regs[0x37] >> 3] / 32768.0) : 0.0;
        mixer->line_l   = (mixer->output_selector & OUTPUT_LINE_L) ? (sb_att_2dbstep_5bits[mixer->regs[0x38] >> 3] / 32768.0) : 0.0;
        mixer->line_r   = (mixer->output_selector & OUTPUT_LINE_R) ? (sb_att_2dbstep_5bits[mixer->regs[0x39] >> 3] / 32768.0) : 0.0;

        mixer->mic     = sb_att_2dbstep_5bits[mixer->regs[0x3a] >> 3] / 32768.0;
        mixer->speaker = sb_att_7dbstep_2bits[(mixer->regs[0x3b] >> 6) & 0x3] / 32768.0;

        mixer->input_gain_L  = (mixer->regs[0x3f] >> 6);
        mixer->input_gain_R  = (mixer->regs[0x40] >> 6);
        mixer->output_gain_L = (double) (1 << (mixer->regs[0x41] >> 6));
        mixer->output_gain_R = (double) (1 << (mixer->regs[0x42] >> 6));

        mixer->bass_l   = mixer->regs[0x46] >> 4;
        mixer->bass_r   = mixer->regs[0x47] >> 4;
        mixer->treble_l = mixer->regs[0x44] >> 4;
        mixer->treble_r = mixer->regs[0x45] >> 4;

        /* TODO: PC Speaker volume, with "output_selector" check? or better not? */
        sb_log("sb_ct1745: Received register WRITE: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
    }
}

uint8_t
sb_ct1745_mixer_read(uint16_t addr, void *priv)
{
    const sb_t              *sb    = (sb_t *) priv;
    const sb_ct1745_mixer_t *mixer = &sb->mixer_sb16;
    uint8_t                  temp;
    uint8_t                  ret = 0xff;

    if (!(addr & 1))
        ret = mixer->index;

    sb_log("sb_ct1745: received register READ: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);

    if ((mixer->index >= 0x30) && (mixer->index <= 0x47))
        ret = mixer->regs[mixer->index];
    else {
        switch (mixer->index) {
            case 0x00:
                ret = mixer->regs[mixer->index];
                break;

            /*SB Pro compatibility*/
            case 0x04:
                ret = ((mixer->regs[0x33] >> 4) & 0x0f) | (mixer->regs[0x32] & 0xf0);
                break;
            case 0x0a:
                ret = (mixer->regs[0x3a] >> 5);
                break;
            case 0x02:
                ret = ((mixer->regs[0x30] >> 4) & 0x0f);
                break;
            case 0x06:
                ret = ((mixer->regs[0x34] >> 4) & 0x0f);
                break;
            case 0x08:
                ret = ((mixer->regs[0x36] >> 4) & 0x0f);
                break;
            case 0x0e:
                ret = 0x02;
                break;
            case 0x22:
                ret = ((mixer->regs[0x31] >> 4) & 0x0f) | (mixer->regs[0x30] & 0xf0);
                break;
            case 0x26:
                ret = ((mixer->regs[0x35] >> 4) & 0x0f) | (mixer->regs[0x34] & 0xf0);
                break;
            case 0x28:
                ret = ((mixer->regs[0x37] >> 4) & 0x0f) | (mixer->regs[0x36] & 0xf0);
                break;
            case 0x2e:
                ret = ((mixer->regs[0x39] >> 4) & 0x0f) | (mixer->regs[0x38] & 0xf0);
                break;

            case 0x48:
                /* Undocumented. The Creative Windows Mixer calls this after calling 3C (input selector),
                   even when writing.
                   Also, the version I have (5.17), does not use the MIDI.L/R input selectors, it uses
                   the volume to mute (Affecting the output, obviously). */
                ret = mixer->regs[mixer->index];
                break;

            case 0x80:
                /* TODO: Unaffected by mixer reset or soft reboot.
                   Enabling multiple bits enables multiple IRQs. */

                switch (sb->dsp.sb_irqnum) {
                    case 2:
                        ret = 1;
                        break;
                    case 5:
                        ret = 2;
                        break;
                    case 7:
                        ret = 4;
                        break;
                    case 10:
                        ret = 8;
                        break;

                    default:
                        break;
                }
                break;

            case 0x81:
                /* TODO: Unaffected by mixer reset or soft reboot.
                   Enabling multiple 8 or 16-bit DMA bits enables multiple DMA channels.
                   Disabling all 8-bit DMA channel bits disables 8-bit DMA requests,
                   including translated 16-bit DMA requests.
                   Disabling all 16-bit DMA channel bits enables translation of 16-bit DMA
                   requests to 8-bit ones, using the selected 8-bit DMA channel. */

                ret = 0;
                switch (sb->dsp.sb_8_dmanum) {
                    case 0:
                        ret |= 1;
                        break;
                    case 1:
                        ret |= 2;
                        break;
                    case 3:
                        ret |= 8;
                        break;

                    default:
                        break;
                }
                switch (sb->dsp.sb_16_dmanum) {
                    case 5:
                        ret |= 0x20;
                        break;
                    case 6:
                        ret |= 0x40;
                        break;
                    case 7:
                        ret |= 0x80;
                        break;
                }
                break;

            case 0x82:
                /* The Interrupt status register, addressed as register 82h on the Mixer register map,
                   is used by the ISR to determine whether the interrupt is meant for it or for some
                   other ISR, in which case it should chain to the previous routine. */
                /* 0 = none, 1 =  digital 8bit or SBMIDI, 2 = digital 16bit, 4 = MPU-401 */
                /* 0x02000 DSP v4.04, 0x4000 DSP v4.05, 0x8000 DSP v4.12.
                   I haven't seen this making any difference, but I'm keeping it for now. */
                /* If QEMU is any indication, then the values are actually 0x20, 0x40, and 0x80. */
                /* http://the.earth.li/~tfm/oldpage/sb_mixer.html - 0x10, 0x20, 0x80. */
                temp = ((sb->dsp.sb_irq8) ? 1 : 0) | ((sb->dsp.sb_irq16) ? 2 : 0) |
                       ((sb->dsp.sb_irq401) ? 4 : 0);
                if (sb->dsp.sb_type >= SBAWE32)
                    ret  = temp | 0x80;
                else
                    ret  = temp | 0x40;
                break;

            case 0x83:
                /* Interrupt mask. */
                ret = mixer->regs[mixer->index];
                break;

            case 0x84:
                /* MPU Control. */
                if (sb->mpu == NULL)
                    ret = 0x02;
                else {
                    if (sb->mpu->addr == 0x330)
                        ret = 0x00;
                    else if (sb->mpu->addr == 0x300)
                        ret = 0x04;
                    else if (sb->mpu->addr == 0)
                        ret = 0x02;
                    else
                        ret = 0x06; /* Should never happen. */
                }
                if (!sb->gameport_addr)
                    ret |= 0x01;
                break;

            case 0x49:    /* Undocumented register used by some Creative drivers. */
            case 0x4a:    /* Undocumented register used by some Creative drivers. */
            case 0x8c:    /* Undocumented register used by some Creative drivers. */
            case 0x8e:    /* Undocumented register used by some Creative drivers. */
            case 0x90:    /* 3D Enhancement switch. */
            case 0xfd:    /* Undocumented register used by some Creative drivers. */
            case 0xfe:    /* Undocumented register used by some Creative drivers. */
                ret = mixer->regs[mixer->index];
                break;

            case 0xff:    /* Undocumented register used by some Creative drivers.
                             This and the upper bits of 0x82 seem to affect the
                             playback volume:
                             - Register FF = FF: Volume playback normal.
                             - Register FF = Not FF: Volume playback low unless
                                             bit 6 of 82h is set. */
                if (sb->dsp.sb_type > SBAWE32)
                    ret = mixer->regs[mixer->index];
                break;

            default:
                sb_log("sb_ct1745: Unknown register READ: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
                break;
        }

        sb_log("CT1745: [R] %02X = %02X\n", mixer->index, ret);
    }

    sb_log("CT1745: read  REG%02X: %02X\n", mixer->index, ret);

    return ret;
}

void
sb_ct1745_mixer_reset(sb_t *sb)
{
    sb_ct1745_mixer_write(4, 0, sb);
    sb_ct1745_mixer_write(5, 0, sb);
}

void
ess_mixer_write(uint16_t addr, uint8_t val, void *priv)
{
    sb_t        *ess   = (sb_t *) priv;
    ess_mixer_t *mixer = &ess->mixer_ess;

    if (!(addr & 1)) {
        mixer->index      = val;
        mixer->regs[0x01] = val;
        if (val == 0x40) {
            mixer->ess_id_str_pos = 0;
        }
    } else {
        if (mixer->index == 0) {
            /* Reset */
            mixer->regs[0x0a] = mixer->regs[0x0c] = 0x00;
            mixer->regs[0x0e]                     = 0x00;
            /* Changed default from -11dB to 0dB */
            mixer->regs[0x04] = mixer->regs[0x22] = 0xee;
            mixer->regs[0x26] = mixer->regs[0x28] = 0xee;
            mixer->regs[0x2e]                     = 0x00;

            /* Initialize ESS regs
             * Defaulting to 0dB instead of the standard -11dB. */
            mixer->regs[0x14] = mixer->regs[0x32] = 0xff;
            mixer->regs[0x36] = mixer->regs[0x38] = 0xff;
            mixer->regs[0x3a]                     = 0x00;
            mixer->regs[0x3c]                     = 0x05;
            mixer->regs[0x3e]                     = 0x00;

            sb_dsp_set_stereo(&ess->dsp, mixer->regs[0x0e] & 2);
        } else {
            mixer->regs[mixer->index] = val;

            switch (mixer->index) {
                /* Compatibility: chain registers 0x02 and 0x22 as well as 0x06 and 0x26 */
                case 0x02:
                case 0x06:
                case 0x08:
                    mixer->regs[mixer->index + 0x20] = ((val & 0xe) << 4) | (val & 0xe);
                    break;

                case 0x0A:
                    {
                        uint8_t mic_vol_2bit = (mixer->regs[0x0a] >> 1) & 0x3;
                        mixer->mic_l = mixer->mic_r = sb_att_7dbstep_2bits[mic_vol_2bit] / 32767.0;
                        mixer->regs[0x1A]           = mic_vol_2bit | (mic_vol_2bit << 2) | (mic_vol_2bit << 4) | (mic_vol_2bit << 6);
                        break;
                    }

                case 0x0C:
                    switch (mixer->regs[0x0C] & 6) {
                        case 2:
                            mixer->input_selector = INPUT_CD_L | INPUT_CD_R;
                            break;
                        case 6:
                            mixer->input_selector = INPUT_LINE_L | INPUT_LINE_R;
                            break;
                        default:
                            mixer->input_selector = INPUT_MIC;
                            break;
                    }
                    mixer->input_filter   = !(mixer->regs[0xC] & 0x20);
                    mixer->in_filter_freq = ((mixer->regs[0xC] & 0x8) == 0) ? 3200 : 8800;
                    break;

                case 0x0E:
                    mixer->output_filter  = !(mixer->regs[0xE] & 0x20);
                    mixer->stereo         = mixer->regs[0xE] & 2;
                    sb_dsp_set_stereo(&ess->dsp, val & 2);
                    break;

                case 0x14:
                    mixer->regs[0x4] = val & 0xee;
                    break;

                case 0x1A:
                    mixer->mic_l = sb_att_1p4dbstep_4bits[(mixer->regs[0x1A] >> 4) & 0xF] / 32767.0;
                    mixer->mic_r = sb_att_1p4dbstep_4bits[mixer->regs[0x1A] & 0xF] / 32767.0;
                    break;

                case 0x1C:
                    if ((mixer->regs[0x1C] & 0x07) == 0x07) {
                        mixer->input_selector = INPUT_MIXER_L | INPUT_MIXER_R;
                    } else if ((mixer->regs[0x1C] & 0x07) == 0x06) {
                        mixer->input_selector = INPUT_LINE_L | INPUT_LINE_R;
                    } else if ((mixer->regs[0x1C] & 0x06) == 0x02) {
                        mixer->input_selector = INPUT_CD_L | INPUT_CD_R;
                    } else if ((mixer->regs[0x1C] & 0x02) == 0) {
                        mixer->input_selector = INPUT_MIC;
                    }
                    break;

                case 0x22:
                case 0x26:
                case 0x28:
                case 0x2E:
                    mixer->regs[mixer->index - 0x20] = (val & 0xe);
                    mixer->regs[mixer->index + 0x10] = val;
                    break;

                /* More compatibility:
                   SoundBlaster Pro selects register 020h for 030h, 022h for 032h,
                   026h for 036h, and 028h for 038h. */
                case 0x30:
                case 0x32:
                case 0x36:
                case 0x38:
                case 0x3e:
                    mixer->regs[mixer->index - 0x10] = (val & 0xee);
                    break;

                case 0x3a:
                case 0x3c:
                    break;

                case 0x00:
                case 0x04:
                    break;

                case 0x64:
                    mixer->regs[mixer->index] &= ~0x8;
                    break;

                case 0x40:
                    {
                        uint16_t mpu401_base_addr = 0x300 | ((mixer->regs[0x40] << 1) & 0x30);
                        gameport_remap(ess->gameport, !(mixer->regs[0x40] & 0x2) ? 0x00 : 0x200);

                        if (ess->dsp.sb_subtype != SB_SUBTYPE_ESS_ES1688) {
                            /* Not on ES1688. */
                            io_removehandler(0x0388, 0x0004,
                                             ess->opl.read, NULL, NULL,
                                             ess->opl.write, NULL, NULL,
                                             ess->opl.priv);
                            if ((mixer->regs[0x40] & 0x1) != 0) {
                                io_sethandler(0x0388, 0x0004,
                                              ess->opl.read, NULL, NULL,
                                              ess->opl.write, NULL, NULL,
                                              ess->opl.priv);
                            }
                        }
                        switch ((mixer->regs[0x40] >> 5) & 0x7) {
                            case 0:
                                mpu401_change_addr(ess->mpu, 0x00);
                                mpu401_setirq(ess->mpu, -1);
                                break;
                            case 1:
                                mpu401_change_addr(ess->mpu, mpu401_base_addr);
                                mpu401_setirq(ess->mpu, -1);
                                break;
                            case 2:
                                mpu401_change_addr(ess->mpu, mpu401_base_addr);
                                mpu401_setirq(ess->mpu, ess->dsp.sb_irqnum);
                                break;
                            case 3:
                                mpu401_change_addr(ess->mpu, mpu401_base_addr);
                                mpu401_setirq(ess->mpu, 11);
                                break;
                            case 4:
                                mpu401_change_addr(ess->mpu, mpu401_base_addr);
                                mpu401_setirq(ess->mpu, 9);
                                break;
                            case 5:
                                mpu401_change_addr(ess->mpu, mpu401_base_addr);
                                mpu401_setirq(ess->mpu, 5);
                                break;
                            case 6:
                                mpu401_change_addr(ess->mpu, mpu401_base_addr);
                                mpu401_setirq(ess->mpu, 7);
                                break;
                            case 7:
                                mpu401_change_addr(ess->mpu, mpu401_base_addr);
                                mpu401_setirq(ess->mpu, 10);
                                break;
                        }
                        break;
                    }

                default:
                    sb_log("ess: Unknown mixer register WRITE: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
                    break;
            }
        }

        mixer->voice_l  = sb_att_2dbstep_4bits[(mixer->regs[0x14] >> 4) & 0x0F] / 32767.0;
        mixer->voice_r  = sb_att_2dbstep_4bits[mixer->regs[0x14] & 0x0F] / 32767.0;
        mixer->master_l = sb_att_2dbstep_4bits[(mixer->regs[0x32] >> 4) & 0x0F] / 32767.0;
        mixer->master_r = sb_att_2dbstep_4bits[mixer->regs[0x32] & 0x0F] / 32767.0;
        mixer->fm_l     = sb_att_2dbstep_4bits[(mixer->regs[0x36] >> 4) & 0x0F] / 32767.0;
        mixer->fm_r     = sb_att_2dbstep_4bits[mixer->regs[0x36] & 0x0F] / 32767.0;
        mixer->cd_l     = sb_att_2dbstep_4bits[(mixer->regs[0x38] >> 4) & 0x0F] / 32767.0;
        mixer->cd_r     = sb_att_2dbstep_4bits[mixer->regs[0x38] & 0x0F] / 32767.0;
        mixer->auxb_l   = sb_att_2dbstep_4bits[(mixer->regs[0x3a] >> 4) & 0x0F] / 32767.0;
        mixer->auxb_r   = sb_att_2dbstep_4bits[mixer->regs[0x3a] & 0x0F] / 32767.0;
        mixer->line_l   = sb_att_2dbstep_4bits[(mixer->regs[0x3e] >> 4) & 0x0F] / 32767.0;
        mixer->line_r   = sb_att_2dbstep_4bits[mixer->regs[0x3e] & 0x0F] / 32767.0;
        mixer->speaker  = sb_att_3dbstep_3bits[mixer->regs[0x3c] & 0x07] / 32767.0;

        /* TODO: PC Speaker volume */
    }
}

uint8_t
ess_mixer_read(uint16_t addr, void *priv)
{
    sb_t        *ess   = (sb_t *) priv;
    ess_mixer_t *mixer = &ess->mixer_ess;

    if (!(addr & 1))
        return mixer->index;

    switch (mixer->index) {
        case 0x00:
        case 0x04:
        case 0x0a:
        case 0x0c:
        case 0x0e:
        case 0x14:
        case 0x22:
        case 0x26:
        case 0x28:
        case 0x2e:
        case 0x02:
        case 0x06:
        case 0x30:
        case 0x32:
        case 0x36:
        case 0x38:
        case 0x3e:
            return mixer->regs[mixer->index];

        case 0x40:
            if (ess->dsp.sb_subtype != SB_SUBTYPE_ESS_ES1688) {
                uint8_t val = mixer->ess_id_str[mixer->ess_id_str_pos];
                mixer->ess_id_str_pos++;
                if (mixer->ess_id_str_pos >= 4)
                    mixer->ess_id_str_pos = 0;
                return val;
            } else {
                return mixer->regs[mixer->index];
            }

        default:
            sb_log("ess: Unknown mixer register READ: %02X\t%02X\n", mixer->index, mixer->regs[mixer->index]);
            break;
    }

    return 0x0a;
}

void
ess_mixer_reset(sb_t *ess)
{
    ess_mixer_write(4, 0, ess);
    ess_mixer_write(5, 0, ess);
}

uint8_t
sb_mcv_read(int port, void *priv)
{
    const sb_t *sb = (sb_t *) priv;

    sb_log("sb_mcv_read: port=%04x\n", port);

    return sb->pos_regs[port & 7];
}

void
sb_mcv_write(int port, uint8_t val, void *priv)
{
    uint16_t addr;
    sb_t    *sb = (sb_t *) priv;

    if (port < 0x102)
        return;

    sb_log("sb_mcv_write: port=%04x val=%02x\n", port, val);

    addr = sb_mcv_addr[sb->pos_regs[4] & 7];
    if (sb->opl_enabled) {
        io_removehandler(addr + 8, 0x0002,
                         sb->opl.read, NULL, NULL,
                         sb->opl.write, NULL, NULL,
                         sb->opl.priv);
        io_removehandler(0x0388, 0x0002,
                         sb->opl.read, NULL, NULL,
                         sb->opl.write, NULL, NULL,
                         sb->opl.priv);
    }
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    sb_dsp_setaddr(&sb->dsp, 0);

    sb->pos_regs[port & 7] = val;

    if (sb->pos_regs[2] & 1) {
        addr = sb_mcv_addr[sb->pos_regs[4] & 7];

        if (sb->opl_enabled) {
            io_sethandler(addr + 8, 0x0002,
                          sb->opl.read, NULL, NULL,
                          sb->opl.write, NULL, NULL,
                          sb->opl.priv);
            io_sethandler(0x0388, 0x0002,
                          sb->opl.read, NULL, NULL,
                          sb->opl.write, NULL, NULL,
                          sb->opl.priv);
        }
        /* DSP I/O handler is activated in sb_dsp_setaddr */
        sb_dsp_setaddr(&sb->dsp, addr);
    }
}

uint8_t
sb_mcv_feedb(void *priv)
{
    const sb_t *sb = (sb_t *) priv;

    return (sb->pos_regs[2] & 1);
}

static uint8_t
sb_pro_mcv_read(int port, void *priv)
{
    const sb_t   *sb  = (sb_t *) priv;
    uint8_t       ret = sb->pos_regs[port & 7];

    sb_log("sb_pro_mcv_read: port=%04x ret=%02x\n", port, ret);

    return ret;
}

static void
sb_pro_mcv_write(int port, uint8_t val, void *priv)
{
    uint16_t addr;
    sb_t    *sb = (sb_t *) priv;

    if (port < 0x102)
        return;

    sb_log("sb_pro_mcv_write: port=%04x val=%02x\n", port, val);

    addr = (sb->pos_regs[2] & 0x20) ? 0x220 : 0x240;

    io_removehandler(addr, 0x0004,
                     sb->opl.read, NULL, NULL,
                     sb->opl.write, NULL, NULL,
                     sb->opl.priv);
    io_removehandler(addr + 8, 0x0002,
                     sb->opl.read, NULL, NULL,
                     sb->opl.write, NULL, NULL,
                     sb->opl.priv);
    io_removehandler(0x0388, 0x0004,
                     sb->opl.read, NULL, NULL,
                     sb->opl.write, NULL, NULL,
                     sb->opl.priv);
    io_removehandler(addr + 4, 0x0002,
                     sb_ct1345_mixer_read, NULL, NULL,
                     sb_ct1345_mixer_write, NULL, NULL,
                     sb);
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    sb_dsp_setaddr(&sb->dsp, 0);

    sb->pos_regs[port & 7] = val;

    if (sb->pos_regs[2] & 1) {
        addr = (sb->pos_regs[2] & 0x20) ? 0x220 : 0x240;

        io_sethandler(addr, 0x0004,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
        io_sethandler(addr + 8, 0x0002,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
        io_sethandler(0x0388, 0x0004,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL, sb->opl.priv);
        io_sethandler(addr + 4, 0x0002,
                      sb_ct1345_mixer_read, NULL, NULL,
                      sb_ct1345_mixer_write, NULL, NULL,
                      sb);
        /* DSP I/O handler is activated in sb_dsp_setaddr */
        sb_dsp_setaddr(&sb->dsp, addr);
    }

    sb_dsp_setirq(&sb->dsp, sb_pro_mcv_irqs[(sb->pos_regs[5] >> 4) & 3]);
    sb_dsp_setdma8(&sb->dsp, sb->pos_regs[4] & 3);
}

static uint8_t
sb_16_reply_mca_read(int port, void *priv)
{
    const sb_t   *sb  = (sb_t *) priv;
    uint8_t       ret = sb->pos_regs[port & 7];

    sb_log("sb_16_reply_mca_read: port=%04x ret=%02x\n", port, ret);

    return ret;
}

static void
sb_16_reply_mca_write(int port, uint8_t val, void *priv)
{
    uint16_t addr;
    uint16_t mpu401_addr;
    int      low_dma;
    int      high_dma;
    sb_t    *sb = (sb_t *) priv;

    if (port < 0x102)
        return;

    sb_log("sb_16_reply_mca_write: port=%04x val=%02x\n", port, val);

    switch (sb->pos_regs[2] & 0xc4) {
        case 4:
            addr = 0x220;
            break;
        case 0x44:
            addr = 0x240;
            break;
        case 0x84:
            addr = 0x260;
            break;
        case 0xc4:
            addr = 0x280;
            break;
        case 0:
        default:
            addr = 0;
            break;
    }

    if (addr) {
        io_removehandler(addr, 0x0004,
                         sb->opl.read, NULL, NULL,
                         sb->opl.write, NULL, NULL,
                         sb->opl.priv);
        io_removehandler(addr + 8, 0x0002,
                         sb->opl.read, NULL, NULL,
                         sb->opl.write, NULL, NULL,
                         sb->opl.priv);
        io_removehandler(0x0388, 0x0004,
                         sb->opl.read, NULL, NULL,
                         sb->opl.write, NULL, NULL,
                         sb->opl.priv);
        io_removehandler(addr + 4, 0x0002,
                         sb_ct1745_mixer_read, NULL, NULL,
                         sb_ct1745_mixer_write, NULL, NULL,
                         sb);
    }

    /* DSP I/O handler is activated in sb_dsp_setaddr */
    sb_dsp_setaddr(&sb->dsp, 0);
    mpu401_change_addr(sb->mpu, 0);
    gameport_remap(sb->gameport, 0);

    sb->pos_regs[port & 7] = val;

    if (sb->pos_regs[2] & 1) {
        switch (sb->pos_regs[2] & 0xc4) {
            case 4:
                addr = 0x220;
                break;
            case 0x44:
                addr = 0x240;
                break;
            case 0x84:
                addr = 0x260;
                break;
            case 0xc4:
                addr = 0x280;
                break;
            case 0:
            default:
                addr = 0;
                break;
        }
        switch (sb->pos_regs[2] & 0x18) {
            case 8:
                mpu401_addr = 0x330;
                break;
            case 0x18:
                mpu401_addr = 0x300;
                break;
            case 0:
            default:
                mpu401_addr = 0;
                break;
        }

        if (addr) {
            io_sethandler(addr, 0x0004,
                          sb->opl.read, NULL, NULL,
                          sb->opl.write, NULL, NULL,
                          sb->opl.priv);
            io_sethandler(addr + 8, 0x0002,
                          sb->opl.read, NULL, NULL,
                          sb->opl.write, NULL, NULL,
                          sb->opl.priv);
            io_sethandler(0x0388, 0x0004,
                          sb->opl.read, NULL, NULL,
                          sb->opl.write, NULL, NULL, sb->opl.priv);
            io_sethandler(addr + 4, 0x0002,
                          sb_ct1745_mixer_read, NULL, NULL,
                          sb_ct1745_mixer_write, NULL, NULL,
                          sb);
        }

        /* DSP I/O handler is activated in sb_dsp_setaddr */
        sb_dsp_setaddr(&sb->dsp, addr);
        mpu401_change_addr(sb->mpu, mpu401_addr);
        gameport_remap(sb->gameport, (sb->pos_regs[2] & 0x20) ? 0x200 : 0);
    }

    switch (sb->pos_regs[4] & 0x60) {
        case 0x20:
            sb_dsp_setirq(&sb->dsp, 5);
            break;
        case 0x40:
            sb_dsp_setirq(&sb->dsp, 7);
            break;
        case 0x60:
            sb_dsp_setirq(&sb->dsp, 10);
            break;

        default:
            break;
    }

    low_dma  = sb->pos_regs[3] & 3;
    high_dma = (sb->pos_regs[3] >> 4) & 7;
    if (!high_dma)
        high_dma = low_dma;

    sb_dsp_setdma8(&sb->dsp, low_dma);
    sb_dsp_setdma16(&sb->dsp, high_dma);
}

void
sb_vibra16s_onboard_relocate_base(uint16_t new_addr, void *priv)
{
    sb_t    *sb   = (sb_t *) priv;
    uint16_t addr = sb->dsp.sb_addr;

    io_removehandler(addr, 0x0004,
                     sb->opl.read, NULL, NULL,
                     sb->opl.write, NULL, NULL,
                     sb->opl.priv);
    io_removehandler(addr + 8, 0x0002,
                     sb->opl.read, NULL, NULL,
                     sb->opl.write, NULL, NULL,
                     sb->opl.priv);
    io_removehandler(addr + 4, 0x0002,
                     sb_ct1745_mixer_read, NULL, NULL,
                     sb_ct1745_mixer_write, NULL, NULL,
                     sb);

    sb_dsp_setaddr(&sb->dsp, 0);

    addr = new_addr;

    io_sethandler(addr, 0x0004,
                  sb->opl.read, NULL, NULL,
                  sb->opl.write, NULL, NULL,
                  sb->opl.priv);
    io_sethandler(addr + 8, 0x0002,
                  sb->opl.read, NULL, NULL,
                  sb->opl.write, NULL, NULL,
                  sb->opl.priv);
    io_sethandler(addr + 4, 0x0002,
                  sb_ct1745_mixer_read, NULL, NULL,
                  sb_ct1745_mixer_write, NULL, NULL,
                  sb);

    sb_dsp_setaddr(&sb->dsp, addr);
}

static void
sb_16_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    sb_t    *sb   = (sb_t *) priv;
    uint16_t addr = sb->dsp.sb_addr;
    uint8_t  val;

    switch (ld) {
        case 0: /* Audio */
            io_removehandler(addr, 0x0004,
                             sb->opl.read, NULL, NULL,
                             sb->opl.write, NULL, NULL,
                             sb->opl.priv);
            io_removehandler(addr + 8, 0x0002,
                             sb->opl.read, NULL, NULL,
                             sb->opl.write, NULL, NULL,
                             sb->opl.priv);
            io_removehandler(addr + 4, 0x0002,
                             sb_ct1745_mixer_read, NULL, NULL,
                             sb_ct1745_mixer_write, NULL, NULL,
                             sb);

            addr = sb->opl_pnp_addr;
            if (addr) {
                sb->opl_pnp_addr = 0;
                io_removehandler(addr, 0x0004,
                                 sb->opl.read, NULL, NULL,
                                 sb->opl.write, NULL, NULL,
                                 sb->opl.priv);
            }

            sb_dsp_setaddr(&sb->dsp, 0);
            sb_dsp_setirq(&sb->dsp, 0);
            sb_dsp_setdma8(&sb->dsp, ISAPNP_DMA_DISABLED);
            sb_dsp_setdma16(&sb->dsp, ISAPNP_DMA_DISABLED);

            mpu401_change_addr(sb->mpu, 0);

            if (config->activate) {
                addr = config->io[0].base;
                if (addr != ISAPNP_IO_DISABLED) {
                    io_sethandler(addr, 0x0004,
                                  sb->opl.read, NULL, NULL,
                                  sb->opl.write, NULL, NULL,
                                  sb->opl.priv);
                    io_sethandler(addr + 8, 0x0002,
                                  sb->opl.read, NULL, NULL,
                                  sb->opl.write, NULL, NULL,
                                  sb->opl.priv);
                    io_sethandler(addr + 4, 0x0002,
                                  sb_ct1745_mixer_read, NULL, NULL,
                                  sb_ct1745_mixer_write, NULL, NULL,
                                  sb);

                    sb_dsp_setaddr(&sb->dsp, addr);
                }

                addr = config->io[1].base;
                if (addr != ISAPNP_IO_DISABLED)
                    mpu401_change_addr(sb->mpu, addr);

                addr = config->io[2].base;
                if (addr != ISAPNP_IO_DISABLED) {
                    sb->opl_pnp_addr = addr;
                    io_sethandler(addr, 0x0004,
                                  sb->opl.read, NULL, NULL,
                                  sb->opl.write, NULL, NULL,
                                  sb->opl.priv);
                }

                val = config->irq[0].irq;
                if (val != ISAPNP_IRQ_DISABLED)
                    sb_dsp_setirq(&sb->dsp, val);

                val = config->dma[0].dma;
                if (val != ISAPNP_DMA_DISABLED)
                    sb_dsp_setdma8(&sb->dsp, val);

                val = config->dma[1].dma;
                sb_dsp_setdma16_enabled(&sb->dsp, val != ISAPNP_DMA_DISABLED);
                sb_dsp_setdma16_translate(&sb->dsp, val < ISAPNP_DMA_DISABLED);
                if (val != ISAPNP_DMA_DISABLED) {
                    if (sb->dsp.sb_16_dma_supported)
                        sb_dsp_setdma16(&sb->dsp, val);
                    else
                        sb_dsp_setdma16_8(&sb->dsp, val);
                }
            }

            break;

        case 1: /* IDE */
            ide_pnp_config_changed(0, config, (void *) 3);
            break;

        case 2: /* Reserved (16) / WaveTable (32+) */
            if (sb->dsp.sb_type > SB16)
                emu8k_change_addr(&sb->emu8k, (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) ? config->io[0].base : 0);
            break;

        case 3: /* Game */
            gameport_remap(sb->gameport, (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) ? config->io[0].base : 0);
            break;

        case 4: /* StereoEnhance (32) */
            break;

        default:
            break;
    }
}

static void
sb_vibra16_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    sb_t    *sb   = (sb_t *) priv;

    switch (ld) {
        case 0: /* Audio */
        case 1: /* Game */
            sb_16_pnp_config_changed(ld * 3, config, sb);
            break;

        default:
            break;
    }
}

static void
sb_awe32_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    sb_t *sb = (sb_t *) priv;

    switch (ld) {
        case 0: /* Audio */
        case 1: /* IDE */
            sb_16_pnp_config_changed(ld, config, sb);
            break;

        case 2: /* Game */
        case 3: /* WaveTable */
            sb_16_pnp_config_changed(ld ^ 1, config, sb);
            break;

        default:
            break;
    }
}

static void
sb_awe64_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    sb_t *sb = (sb_t *) priv;

    switch (ld) {
        case 0: /* Audio */
        case 2: /* WaveTable */
            sb_16_pnp_config_changed(ld, config, sb);
            break;

        case 1: /* Game */
        case 3: /* IDE */
            sb_16_pnp_config_changed(ld ^ 2, config, sb);
            break;

        default:
            break;
    }
}

static void
sb_awe64_gold_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    sb_t *sb = (sb_t *) priv;

    switch (ld) {
        case 0: /* Audio */
        case 2: /* WaveTable */
            sb_16_pnp_config_changed(ld, config, sb);
            break;

        case 1: /* Game */
            sb_16_pnp_config_changed(3, config, sb);
            break;

        default:
            break;
    }
}

void *
sb_1_init(UNUSED(const device_t *info))
{
    /* SB1/2 port mappings, 210h to 260h in 10h steps
       2x0 to 2x3 -> CMS chip
       2x6, 2xA, 2xC, 2xE -> DSP chip
       2x8, 2x9, 388 and 389 FM chip */
    sb_t    *sb   = malloc(sizeof(sb_t));
    uint16_t addr = device_get_config_hex16("base");
    memset(sb, 0, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled)
        fm_driver_get(FM_YM3812, &sb->opl);

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb_dsp_init(&sb->dsp, SB1, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, addr);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    if (sb->opl_enabled) {
        io_sethandler(addr + 8, 0x0002,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
        io_sethandler(0x0388, 0x0002,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
    }

    sb->cms_enabled = 1;
    memset(&sb->cms, 0, sizeof(cms_t));
    io_sethandler(addr, 0x0004,
                  cms_read, NULL, NULL,
                  cms_write, NULL, NULL,
                  &sb->cms);

    sb->mixer_enabled = 0;
    sound_add_handler(sb_get_buffer_sb2, sb);
    if (sb->opl_enabled)
        music_add_handler(sb_get_music_buffer_sb2, sb);
    sound_set_cd_audio_filter(sb2_filter_cd_audio, sb);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}

void *
sb_15_init(UNUSED(const device_t *info))
{
    /* SB1/2 port mappings, 210h to 260h in 10h steps
       2x0 to 2x3 -> CMS chip
       2x6, 2xA, 2xC, 2xE -> DSP chip
       2x8, 2x9, 388 and 389 FM chip */
    sb_t    *sb   = malloc(sizeof(sb_t));
    uint16_t addr = device_get_config_hex16("base");
    memset(sb, 0, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled)
        fm_driver_get(FM_YM3812, &sb->opl);

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb_dsp_init(&sb->dsp, SB15, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, addr);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    if (sb->opl_enabled) {
        io_sethandler(addr + 8, 0x0002,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
        io_sethandler(0x0388, 0x0002,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
    }

    sb->cms_enabled = device_get_config_int("cms");
    if (sb->cms_enabled) {
        memset(&sb->cms, 0, sizeof(cms_t));
        io_sethandler(addr, 0x0004,
                      cms_read, NULL, NULL,
                      cms_write, NULL, NULL,
                      &sb->cms);
    }

    sb->mixer_enabled = 0;
    sound_add_handler(sb_get_buffer_sb2, sb);
    if (sb->opl_enabled)
        music_add_handler(sb_get_music_buffer_sb2, sb);
    sound_set_cd_audio_filter(sb2_filter_cd_audio, sb);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}

void *
sb_mcv_init(UNUSED(const device_t *info))
{
    /* SB1/2 port mappings, 210h to 260h in 10h steps
       2x6, 2xA, 2xC, 2xE -> DSP chip
       2x8, 2x9, 388 and 389 FM chip */
    sb_t *sb = malloc(sizeof(sb_t));
    memset(sb, 0, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled)
        fm_driver_get(FM_YM3812, &sb->opl);

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb_dsp_init(&sb->dsp, SB15, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, 0);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));

    sb->mixer_enabled = 0;
    sound_add_handler(sb_get_buffer_sb2, sb);
    if (sb->opl_enabled)
        music_add_handler(sb_get_music_buffer_sb2, sb);
    sound_set_cd_audio_filter(sb2_filter_cd_audio, sb);

    /* I/O handlers activated in sb_mcv_write */
    mca_add(sb_mcv_read, sb_mcv_write, sb_mcv_feedb, NULL, sb);
    sb->pos_regs[0] = 0x84;
    sb->pos_regs[1] = 0x50;

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}

void *
sb_2_init(UNUSED(const device_t *info))
{
    /* SB2 port mappings, 220h or 240h.
       2x0 to 2x3 -> CMS chip
       2x6, 2xA, 2xC, 2xE -> DSP chip
       2x8, 2x9, 388 and 389 FM chip
       "CD version" also uses 250h or 260h for
       2x0 to 2x3 -> CDROM interface
       2x4 to 2x5 -> Mixer interface */
    /* My SB 2.0 mirrors the OPL2 at ports 2x0/2x1. Presumably this mirror is disabled when the
       CMS chips are present.
       This mirror may also exist on SB 1.5 & MCV, however I am unable to test this. It shouldn't
       exist on SB 1.0 as the CMS chips are always present there. Syndicate requires this mirror
       for music to play.*/
    sb_t    *sb         = malloc(sizeof(sb_t));
    uint16_t addr       = device_get_config_hex16("base");
    uint16_t mixer_addr = device_get_config_int("mixaddr");

    memset(sb, 0, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled)
        fm_driver_get(FM_YM3812, &sb->opl);

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb_dsp_init(&sb->dsp, SB2, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, addr);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
    if (mixer_addr > 0x000)
        sb_ct1335_mixer_reset(sb);

    sb->cms_enabled = device_get_config_int("cms");
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    if (sb->opl_enabled) {
        if (!sb->cms_enabled) {
            io_sethandler(addr, 0x0002,
                          sb->opl.read, NULL, NULL,
                          sb->opl.write, NULL, NULL,
                          sb->opl.priv);
        }
        io_sethandler(addr + 8, 0x0002,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
        io_sethandler(0x0388, 0x0002,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
    }

    if (sb->cms_enabled) {
        memset(&sb->cms, 0, sizeof(cms_t));
        io_sethandler(addr, 0x0004,
                      cms_read, NULL, NULL,
                      cms_write, NULL, NULL,
                      &sb->cms);
    }

    if (mixer_addr > 0x000) {
        sb->mixer_enabled = 1;
        io_sethandler(mixer_addr + 4, 0x0002,
                      sb_ct1335_mixer_read, NULL, NULL,
                      sb_ct1335_mixer_write, NULL, NULL,
                      sb);
    } else
        sb->mixer_enabled = 0;
    sound_add_handler(sb_get_buffer_sb2, sb);
    if (sb->opl_enabled)
        music_add_handler(sb_get_music_buffer_sb2, sb);
    sound_set_cd_audio_filter(sb2_filter_cd_audio, sb);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}

static uint8_t
sb_pro_v1_opl_read(uint16_t port, void *priv)
{
    sb_t *sb = (sb_t *) priv;

    cycles -= ((int) (isa_timing * 8));

    (void) sb->opl2.read(port, sb->opl2.priv); // read, but ignore
    return (sb->opl.read(port, sb->opl.priv));
}

static void
sb_pro_v1_opl_write(uint16_t port, uint8_t val, void *priv)
{
    sb_t *sb = (sb_t *) priv;

    sb->opl.write(port, val, sb->opl.priv);
    sb->opl2.write(port, val, sb->opl2.priv);
}

static void *
sb_pro_v1_init(UNUSED(const device_t *info))
{
    /* SB Pro port mappings, 220h or 240h.
       2x0 to 2x3 -> FM chip, Left and Right (9*2 voices)
       2x4 to 2x5 -> Mixer interface
       2x6, 2xA, 2xC, 2xE -> DSP chip
       2x8, 2x9, 388 and 389 FM chip (9 voices)
       2x0+10 to 2x0+13 CDROM interface. */
    sb_t    *sb   = malloc(sizeof(sb_t));
    uint16_t addr = device_get_config_hex16("base");
    memset(sb, 0, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled) {
        fm_driver_get(FM_YM3812, &sb->opl);
        sb->opl.set_do_cycles(sb->opl.priv, 0);
        fm_driver_get(FM_YM3812, &sb->opl2);
        sb->opl2.set_do_cycles(sb->opl2.priv, 0);
    }

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb_dsp_init(&sb->dsp, SBPRO, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, addr);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
    sb_ct1345_mixer_reset(sb);
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    if (sb->opl_enabled) {
        io_sethandler(addr, 0x0002,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
        io_sethandler(addr + 2, 0x0002,
                      sb->opl2.read, NULL, NULL,
                      sb->opl2.write, NULL, NULL,
                      sb->opl2.priv);
        io_sethandler(addr + 8, 0x0002,
                      sb_pro_v1_opl_read, NULL, NULL,
                      sb_pro_v1_opl_write, NULL, NULL,
                      sb);
        io_sethandler(0x0388, 0x0002,
                      sb_pro_v1_opl_read, NULL, NULL,
                      sb_pro_v1_opl_write, NULL, NULL,
                      sb);
    }

    sb->mixer_enabled = 1;
    io_sethandler(addr + 4, 0x0002,
                  sb_ct1345_mixer_read, NULL, NULL,
                  sb_ct1345_mixer_write, NULL, NULL,
                  sb);
    sound_add_handler(sb_get_buffer_sbpro, sb);
    if (sb->opl_enabled)
        music_add_handler(sb_get_music_buffer_sbpro, sb);
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, sb);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}

static void *
sb_pro_v2_init(UNUSED(const device_t *info))
{
    /* SB Pro 2 port mappings, 220h or 240h.
       2x0 to 2x3 -> FM chip (18 voices)
       2x4 to 2x5 -> Mixer interface
       2x6, 2xA, 2xC, 2xE -> DSP chip
       2x8, 2x9, 388 and 389 FM chip (9 voices)
       2x0+10 to 2x0+13 CDROM interface. */
    sb_t    *sb   = malloc(sizeof(sb_t));
    uint16_t addr = device_get_config_hex16("base");
    memset(sb, 0, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled)
        fm_driver_get(FM_YMF262, &sb->opl);

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb_dsp_init(&sb->dsp, SBPRO2, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, addr);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
    sb_ct1345_mixer_reset(sb);
    /* DSP I/O handler is activated in sb_dsp_setaddr */
    if (sb->opl_enabled) {
        io_sethandler(addr, 0x0004,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
        io_sethandler(addr + 8, 0x0002,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
        io_sethandler(0x0388, 0x0004,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
    }

    sb->mixer_enabled = 1;
    io_sethandler(addr + 4, 0x0002,
                  sb_ct1345_mixer_read, NULL, NULL,
                  sb_ct1345_mixer_write, NULL, NULL,
                  sb);
    sound_add_handler(sb_get_buffer_sbpro, sb);
    if (sb->opl_enabled)
        music_add_handler(sb_get_music_buffer_sbpro, sb);
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, sb);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}

static void *
sb_pro_mcv_init(UNUSED(const device_t *info))
{
    /* SB Pro MCV port mappings, 220h or 240h.
       2x0 to 2x3 -> FM chip, Left and Right (18 voices)
       2x4 to 2x5 -> Mixer interface
       2x6, 2xA, 2xC, 2xE -> DSP chip
       2x8, 2x9, 388 and 389 FM chip (9 voices) */
    sb_t *sb = malloc(sizeof(sb_t));
    memset(sb, 0, sizeof(sb_t));

    sb->opl_enabled = 1;
    fm_driver_get(FM_YMF262, &sb->opl);

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb_dsp_init(&sb->dsp, SBPRO2, SB_SUBTYPE_DEFAULT, sb);
    sb_ct1345_mixer_reset(sb);

    sb->mixer_enabled = 1;
    sound_add_handler(sb_get_buffer_sbpro, sb);
    if (sb->opl_enabled)
        music_add_handler(sb_get_music_buffer_sbpro, sb);
    sound_set_cd_audio_filter(sbpro_filter_cd_audio, sb);

    /* I/O handlers activated in sb_pro_mcv_write */
    mca_add(sb_pro_mcv_read, sb_pro_mcv_write, sb_mcv_feedb, NULL, sb);
    sb->pos_regs[0] = 0x03;
    sb->pos_regs[1] = 0x51;

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    return sb;
}

static void *
sb_pro_compat_init(UNUSED(const device_t *info))
{
    sb_t *sb = malloc(sizeof(sb_t));
    memset(sb, 0, sizeof(sb_t));

    fm_driver_get(FM_YMF262, &sb->opl);

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb_dsp_init(&sb->dsp, SBPRO2, SB_SUBTYPE_DEFAULT, sb);
    sb_ct1345_mixer_reset(sb);

    sb->mixer_enabled = 1;
    sound_add_handler(sb_get_buffer_sbpro, sb);
    if (sb->opl_enabled)
        music_add_handler(sb_get_music_buffer_sbpro, sb);

    sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
    memset(sb->mpu, 0, sizeof(mpu_t));
    mpu401_init(sb->mpu, 0, 0, M_UART, 1);
    sb_dsp_set_mpu(&sb->dsp, sb->mpu);

    return sb;
}

static void *
sb_16_init(UNUSED(const device_t *info))
{
    sb_t    *sb       = malloc(sizeof(sb_t));
    uint16_t addr     = device_get_config_hex16("base");
    uint16_t mpu_addr = device_get_config_hex16("base401");

    memset(sb, 0x00, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled)
        fm_driver_get(info->local, &sb->opl);

    sb_dsp_set_real_opl(&sb->dsp, (info->local != FM_YMF289B));
    sb_dsp_init(&sb->dsp, (info->local == FM_YMF289B) ? SBAWE32PNP : SB16, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, addr);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
    sb_dsp_setdma16(&sb->dsp, device_get_config_int("dma16"));
    sb_dsp_setdma16_supported(&sb->dsp, 1);
    sb_dsp_setdma16_enabled(&sb->dsp, 1);
    sb_ct1745_mixer_reset(sb);

    if (sb->opl_enabled) {
        io_sethandler(addr, 0x0004,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
        io_sethandler(addr + 8, 0x0002,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
        io_sethandler(0x0388, 0x0004,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
    }

    sb->mixer_enabled            = 1;
    sb->mixer_sb16.output_filter = 1;
    io_sethandler(addr + 4, 0x0002, sb_ct1745_mixer_read, NULL, NULL,
                  sb_ct1745_mixer_write, NULL, NULL, sb);
    sound_add_handler(sb_get_buffer_sb16_awe32, sb);
    if (sb->opl_enabled) {
        if (info->local == FM_YMF289B)
            sound_add_handler(sb_get_music_buffer_sb16_awe32, sb);
        else
            music_add_handler(sb_get_music_buffer_sb16_awe32, sb);
    }
    sound_set_cd_audio_filter(sb16_awe32_filter_cd_audio, sb);
    if (device_get_config_int("control_pc_speaker"))
        sound_set_pc_speaker_filter(sb16_awe32_filter_pc_speaker, sb);

    if (mpu_addr) {
        sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
        memset(sb->mpu, 0, sizeof(mpu_t));
        mpu401_init(sb->mpu, device_get_config_hex16("base401"), device_get_config_int("irq"), M_UART, device_get_config_int("receive_input401"));
    } else
        sb->mpu = NULL;
    sb_dsp_set_mpu(&sb->dsp, sb->mpu);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    sb->gameport = gameport_add(&gameport_pnp_device);
    sb->gameport_addr = 0x200;
    gameport_remap(sb->gameport, sb->gameport_addr);

    return sb;
}

static void *
sb_16_reply_mca_init(UNUSED(const device_t *info))
{
    sb_t *sb = malloc(sizeof(sb_t));
    memset(sb, 0x00, sizeof(sb_t));

    sb->opl_enabled = 1;
    fm_driver_get(FM_YMF262, &sb->opl);

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb_dsp_init(&sb->dsp, SB16, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setdma16_supported(&sb->dsp, 1);
    sb_dsp_setdma16_enabled(&sb->dsp, 1);
    sb_ct1745_mixer_reset(sb);

    sb->mixer_enabled            = 1;
    sb->mixer_sb16.output_filter = 1;
    sound_add_handler(sb_get_buffer_sb16_awe32, sb);
    if (sb->opl_enabled)
        music_add_handler(sb_get_music_buffer_sb16_awe32, sb);
    sound_set_cd_audio_filter(sb16_awe32_filter_cd_audio, sb);
    if (device_get_config_int("control_pc_speaker"))
        sound_set_pc_speaker_filter(sb16_awe32_filter_pc_speaker, sb);

    sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
    memset(sb->mpu, 0, sizeof(mpu_t));
    mpu401_init(sb->mpu, 0, 0, M_UART, device_get_config_int("receive_input401"));
    sb_dsp_set_mpu(&sb->dsp, sb->mpu);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    sb->gameport = gameport_add(&gameport_device);

    /* I/O handlers activated in sb_pro_mcv_write */
    mca_add(sb_16_reply_mca_read, sb_16_reply_mca_write, sb_mcv_feedb, NULL, sb);
    sb->pos_regs[0] = 0x38;
    sb->pos_regs[1] = 0x51;

    sb->gameport_addr = 0x200;

    return sb;
}

static void *
sb_16_pnp_init(UNUSED(const device_t *info))
{
    sb_t *sb = malloc(sizeof(sb_t));
    memset(sb, 0x00, sizeof(sb_t));

    sb->pnp = 1;

    sb->opl_enabled = 1;
    fm_driver_get(FM_YMF262, &sb->opl);

    sb_dsp_init(&sb->dsp, SB16, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setdma16_supported(&sb->dsp, 1);
    sb_ct1745_mixer_reset(sb);

    sb->mixer_enabled            = 1;
    sb->mixer_sb16.output_filter = 1;
    sound_add_handler(sb_get_buffer_sb16_awe32, sb);
    if (sb->opl_enabled)
        music_add_handler(sb_get_music_buffer_sb16_awe32, sb);
    sound_set_cd_audio_filter(sb16_awe32_filter_cd_audio, sb);
    if (device_get_config_int("control_pc_speaker"))
        sound_set_pc_speaker_filter(sb16_awe32_filter_pc_speaker, sb);

    sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
    memset(sb->mpu, 0, sizeof(mpu_t));
    mpu401_init(sb->mpu, 0, 0, M_UART, device_get_config_int("receive_input401"));
    sb_dsp_set_mpu(&sb->dsp, sb->mpu);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    sb->gameport = gameport_add(&gameport_pnp_device);

    device_add(&ide_qua_pnp_device);

    isapnp_add_card(sb_16_pnp_rom, sizeof(sb_16_pnp_rom), sb_16_pnp_config_changed, NULL, NULL, NULL, sb);

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb_dsp_setaddr(&sb->dsp, 0);
    sb_dsp_setirq(&sb->dsp, 0);
    sb_dsp_setdma8(&sb->dsp, ISAPNP_DMA_DISABLED);
    sb_dsp_setdma16(&sb->dsp, ISAPNP_DMA_DISABLED);

    mpu401_change_addr(sb->mpu, 0);
    ide_remove_handlers(3);

    sb->gameport_addr = 0;
    gameport_remap(sb->gameport, 0);

    return sb;
}

static int
sb_vibra16xv_available(void)
{
    return rom_present(PNP_ROM_SB_VIBRA16XV);
}

static int
sb_vibra16c_available(void)
{
    return rom_present(PNP_ROM_SB_VIBRA16C);
}

static void *
sb_vibra16_pnp_init(UNUSED(const device_t *info))
{
    sb_t *sb = malloc(sizeof(sb_t));
    memset(sb, 0x00, sizeof(sb_t));

    sb->pnp = 1;

    sb->opl_enabled = 1;
    fm_driver_get(FM_YMF262, &sb->opl);

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb_dsp_init(&sb->dsp, (info->local == 0) ? SBAWE64 : SBAWE32PNP, SB_SUBTYPE_DEFAULT, sb);
    /* The ViBRA 16XV does 16-bit DMA through 8-bit DMA. */
    sb_dsp_setdma16_supported(&sb->dsp, info->local != 0);
    sb_ct1745_mixer_reset(sb);

    sb->mixer_enabled            = 1;
    sb->mixer_sb16.output_filter = 1;
    sound_add_handler(sb_get_buffer_sb16_awe32, sb);
    if (sb->opl_enabled)
        music_add_handler(sb_get_music_buffer_sb16_awe32, sb);
    sound_set_cd_audio_filter(sb16_awe32_filter_cd_audio, sb);
    if (device_get_config_int("control_pc_speaker"))
        sound_set_pc_speaker_filter(sb16_awe32_filter_pc_speaker, sb);

    sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
    memset(sb->mpu, 0, sizeof(mpu_t));
    mpu401_init(sb->mpu, 0, 0, M_UART, device_get_config_int("receive_input401"));
    sb_dsp_set_mpu(&sb->dsp, sb->mpu);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    sb->gameport = gameport_add(&gameport_pnp_device);

    const char *pnp_rom_file = NULL;
    switch (info->local) {
        case 0:
            pnp_rom_file = PNP_ROM_SB_VIBRA16XV;
            break;

        case 1:
            pnp_rom_file = PNP_ROM_SB_VIBRA16C;
            break;

        default:
            break;
    }

    uint8_t *pnp_rom = NULL;
    if (pnp_rom_file) {
        FILE *fp = rom_fopen(pnp_rom_file, "rb");
        if (fp) {
            if (fread(sb->pnp_rom, 1, 512, fp) == 512)
                pnp_rom = sb->pnp_rom;
            fclose(fp);
        }
    }

    switch (info->local) {
        case 0:
        case 1:
            isapnp_add_card(pnp_rom, sizeof(sb->pnp_rom), sb_vibra16_pnp_config_changed,
                            NULL, NULL, NULL, sb);
            break;

        default:
            break;
    }

    sb_dsp_setaddr(&sb->dsp, 0);
    sb_dsp_setirq(&sb->dsp, 0);
    sb_dsp_setdma8(&sb->dsp, ISAPNP_DMA_DISABLED);
    sb_dsp_setdma16(&sb->dsp, ISAPNP_DMA_DISABLED);

    mpu401_change_addr(sb->mpu, 0);

    sb->gameport_addr = 0;
    gameport_remap(sb->gameport, 0);

    return sb;
}

static void *
sb_16_compat_init(const device_t *info)
{
    sb_t *sb = malloc(sizeof(sb_t));
    memset(sb, 0, sizeof(sb_t));

    fm_driver_get(FM_YMF262, &sb->opl);

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb_dsp_init(&sb->dsp, SB16, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setdma16_supported(&sb->dsp, 1);
    sb_dsp_setdma16_enabled(&sb->dsp, 1);
    sb_ct1745_mixer_reset(sb);

    sb->mixer_enabled = 1;
    sound_add_handler(sb_get_buffer_sb16_awe32, sb);
    if (sb->opl_enabled)
        music_add_handler(sb_get_music_buffer_sb16_awe32, sb);

    sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
    memset(sb->mpu, 0, sizeof(mpu_t));
    mpu401_init(sb->mpu, 0, 0, M_UART, info->local);
    sb_dsp_set_mpu(&sb->dsp, sb->mpu);

    sb->gameport = gameport_add(&gameport_pnp_device);
    sb->gameport_addr = 0x200;
    gameport_remap(sb->gameport, sb->gameport_addr);

    return sb;
}

static int
sb_awe32_available(void)
{
    return rom_present(EMU8K_ROM_PATH);
}

static int
sb_32_pnp_available(void)
{
    return sb_awe32_available() && rom_present(PNP_ROM_SB_32_PNP);
}

static int
sb_awe32_pnp_available(void)
{
    return sb_awe32_available() && rom_present(PNP_ROM_SB_AWE32_PNP);
}

static int
sb_awe64_value_available(void)
{
    return sb_awe32_available() && rom_present(PNP_ROM_SB_AWE64_VALUE);
}

static int
sb_awe64_available(void)
{
    return sb_awe32_available() && rom_present(PNP_ROM_SB_AWE64);
}

static int
sb_awe64_gold_available(void)
{
    return sb_awe32_available() && rom_present(PNP_ROM_SB_AWE64_GOLD);
}

static void *
sb_awe32_init(UNUSED(const device_t *info))
{
    sb_t    *sb          = malloc(sizeof(sb_t));
    uint16_t addr        = device_get_config_hex16("base");
    uint16_t mpu_addr    = device_get_config_hex16("base401");
    uint16_t emu_addr    = device_get_config_hex16("emu_base");
    int      onboard_ram = device_get_config_int("onboard_ram");

    memset(sb, 0x00, sizeof(sb_t));

    sb->opl_enabled = device_get_config_int("opl");
    if (sb->opl_enabled)
        fm_driver_get(FM_YMF262, &sb->opl);

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb_dsp_init(&sb->dsp, SBAWE32, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setaddr(&sb->dsp, addr);
    sb_dsp_setirq(&sb->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&sb->dsp, device_get_config_int("dma"));
    sb_dsp_setdma16(&sb->dsp, device_get_config_int("dma16"));
    sb_dsp_setdma16_supported(&sb->dsp, 1);
    sb_dsp_setdma16_enabled(&sb->dsp, 1);
    sb_ct1745_mixer_reset(sb);

    if (sb->opl_enabled) {
        io_sethandler(addr, 0x0004,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
        io_sethandler(addr + 8, 0x0002,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
        io_sethandler(0x0388, 0x0004,
                      sb->opl.read, NULL, NULL,
                      sb->opl.write, NULL, NULL,
                      sb->opl.priv);
    }

    sb->mixer_enabled            = 1;
    sb->mixer_sb16.output_filter = 1;
    io_sethandler(addr + 4, 0x0002, sb_ct1745_mixer_read, NULL, NULL,
                  sb_ct1745_mixer_write, NULL, NULL, sb);
    sound_add_handler(sb_get_buffer_sb16_awe32, sb);
    if (sb->opl_enabled)
        music_add_handler(sb_get_music_buffer_sb16_awe32, sb);
    sound_set_cd_audio_filter(sb16_awe32_filter_cd_audio, sb);
    if (device_get_config_int("control_pc_speaker"))
        sound_set_pc_speaker_filter(sb16_awe32_filter_pc_speaker, sb);

    if (mpu_addr) {
        sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
        memset(sb->mpu, 0, sizeof(mpu_t));
        mpu401_init(sb->mpu, device_get_config_hex16("base401"), device_get_config_int("irq"), M_UART, device_get_config_int("receive_input401"));
    } else
        sb->mpu = NULL;
    sb_dsp_set_mpu(&sb->dsp, sb->mpu);

    emu8k_init(&sb->emu8k, emu_addr, onboard_ram);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    sb->gameport = gameport_add(&gameport_pnp_device);
    sb->gameport_addr = 0x200;
    gameport_remap(sb->gameport, sb->gameport_addr);

    return sb;
}

static void *
sb_awe32_pnp_init(const device_t *info)
{
    sb_t *sb          = malloc(sizeof(sb_t));
    int   onboard_ram = device_get_config_int("onboard_ram");

    memset(sb, 0x00, sizeof(sb_t));

    sb->pnp = 1;

    sb->opl_enabled = 1;
    fm_driver_get(FM_YMF262, &sb->opl);

    sb_dsp_init(&sb->dsp, ((info->local == 2) || (info->local == 3) || (info->local == 4)) ?
                SBAWE64 : SBAWE32PNP, SB_SUBTYPE_DEFAULT, sb);
    sb_dsp_setdma16_supported(&sb->dsp, 1);
    sb_ct1745_mixer_reset(sb);

    sb_dsp_set_real_opl(&sb->dsp, 1);
    sb->mixer_enabled            = 1;
    sb->mixer_sb16.output_filter = 1;
    sound_add_handler(sb_get_buffer_sb16_awe32, sb);
    if (sb->opl_enabled)
        music_add_handler(sb_get_music_buffer_sb16_awe32, sb);
    sound_set_cd_audio_filter(sb16_awe32_filter_cd_audio, sb);
    if (device_get_config_int("control_pc_speaker"))
        sound_set_pc_speaker_filter(sb16_awe32_filter_pc_speaker, sb);

    sb->mpu = (mpu_t *) malloc(sizeof(mpu_t));
    memset(sb->mpu, 0, sizeof(mpu_t));
    mpu401_init(sb->mpu, 0, 0, M_UART, device_get_config_int("receive_input401"));
    sb_dsp_set_mpu(&sb->dsp, sb->mpu);

    emu8k_init(&sb->emu8k, 0, onboard_ram);

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &sb->dsp);

    sb->gameport = gameport_add(&gameport_pnp_device);

    if ((info->local != 2) && (info->local != 4))
        device_add(&ide_qua_pnp_device);

    const char *pnp_rom_file = NULL;
    switch (info->local) {
        case 0:
            pnp_rom_file = PNP_ROM_SB_32_PNP;
            break;

        case 1:
            pnp_rom_file = PNP_ROM_SB_AWE32_PNP;
            break;

        case 2:
            pnp_rom_file = PNP_ROM_SB_AWE64_VALUE;
            break;

        case 3:
            pnp_rom_file = PNP_ROM_SB_AWE64;
            break;

        case 4:
            pnp_rom_file = PNP_ROM_SB_AWE64_GOLD;
            break;

        default:
            break;
    }

    uint8_t *pnp_rom = NULL;
    if (pnp_rom_file) {
        FILE *fp = rom_fopen(pnp_rom_file, "rb");
        if (fp) {
            if (fread(sb->pnp_rom, 1, 512, fp) == 512)
                pnp_rom = sb->pnp_rom;
            fclose(fp);
        }
    }

    switch (info->local) {
        case 0:
            isapnp_add_card(pnp_rom, sizeof(sb->pnp_rom), sb_16_pnp_config_changed, NULL, NULL, NULL, sb);
            break;

        case 1:
            isapnp_add_card(pnp_rom, sizeof(sb->pnp_rom), sb_awe32_pnp_config_changed, NULL, NULL, NULL, sb);
            break;

        case 3:
            isapnp_add_card(pnp_rom, sizeof(sb->pnp_rom), sb_awe64_pnp_config_changed, NULL, NULL, NULL, sb);
            break;

        case 2:
        case 4:
            isapnp_add_card(pnp_rom, sizeof(sb->pnp_rom), sb_awe64_gold_pnp_config_changed, NULL, NULL, NULL, sb);
            break;

        default:
            break;
    }

    sb_dsp_setaddr(&sb->dsp, 0);
    sb_dsp_setirq(&sb->dsp, 0);
    sb_dsp_setdma8(&sb->dsp, ISAPNP_DMA_DISABLED);
    sb_dsp_setdma16(&sb->dsp, ISAPNP_DMA_DISABLED);

    mpu401_change_addr(sb->mpu, 0);
    if ((info->local != 2) && (info->local != 4))
        ide_remove_handlers(3);

    emu8k_change_addr(&sb->emu8k, 0);

    sb->gameport_addr = 0;

    gameport_remap(sb->gameport, 0);

    return sb;
}

static void *
ess_1688_init(UNUSED(const device_t *info))
{
    sb_t    *ess  = calloc(sizeof(sb_t), 1);
    uint16_t addr = device_get_config_hex16("base");

    fm_driver_get(FM_ESFM, &ess->opl);

    sb_dsp_set_real_opl(&ess->dsp, 1);
    sb_dsp_init(&ess->dsp, SBPRO2, SB_SUBTYPE_ESS_ES1688, ess);
    sb_dsp_setaddr(&ess->dsp, addr);
    sb_dsp_setirq(&ess->dsp, device_get_config_int("irq"));
    sb_dsp_setdma8(&ess->dsp, device_get_config_int("dma"));
    sb_dsp_setdma16_8(&ess->dsp, device_get_config_int("dma"));
    sb_dsp_setdma16_supported(&ess->dsp, 0);
    ess_mixer_reset(ess);

    /* DSP I/O handler is activated in sb_dsp_setaddr */
    {
        io_sethandler(addr, 0x0004,
                      ess->opl.read, NULL, NULL,
                      ess->opl.write, NULL, NULL,
                      ess->opl.priv);
        io_sethandler(addr + 8, 0x0002,
                      ess->opl.read, NULL, NULL,
                      ess->opl.write, NULL, NULL,
                      ess->opl.priv);
        io_sethandler(0x0388, 0x0004,
                      ess->opl.read, NULL, NULL,
                      ess->opl.write, NULL, NULL,
                      ess->opl.priv);
    }

    ess->mixer_enabled = 1;
    io_sethandler(addr + 4, 0x0002,
                  ess_mixer_read, NULL, NULL,
                  ess_mixer_write, NULL, NULL,
                  ess);
    sound_add_handler(sb_get_buffer_ess, ess);
    music_add_handler(sb_get_music_buffer_ess, ess);
    sound_set_cd_audio_filter(ess_filter_cd_audio, ess);

    if (device_get_config_int("receive_input")) {
        midi_in_handler(1, sb_dsp_input_msg, sb_dsp_input_sysex, &ess->dsp);
    }

    ess->mixer_ess.ess_id_str[0] = 0x16;
    ess->mixer_ess.ess_id_str[1] = 0x88;
    ess->mixer_ess.ess_id_str[2] = (addr >> 8) & 0xff;
    ess->mixer_ess.ess_id_str[3] = addr & 0xff;

    ess->mpu = (mpu_t *) calloc(1, sizeof(mpu_t));
    /* NOTE: The MPU is initialized disabled and with no IRQ assigned.
     * It will be later initialized by the guest OS's drivers. */
    mpu401_init(ess->mpu, 0, -1, M_UART, 1);
    sb_dsp_set_mpu(&ess->dsp, ess->mpu);

    ess->gameport      = gameport_add(&gameport_pnp_device);
    ess->gameport_addr = 0x200;
    gameport_remap(ess->gameport, ess->gameport_addr);

    return ess;
}

void
sb_close(void *priv)
{
    sb_t *sb = (sb_t *) priv;
    sb_dsp_close(&sb->dsp);

    free(sb);
}

static void
sb_awe32_close(void *priv)
{
    sb_t *sb = (sb_t *) priv;

    emu8k_close(&sb->emu8k);

    sb_close(sb);
}

void
sb_speed_changed(void *priv)
{
    sb_t *sb = (sb_t *) priv;

    sb_dsp_speed_changed(&sb->dsp);
}

// clang-format off
static const device_config_t sb_config[] = {
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x220,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "0x210",
                .value = 0x210
            },
            {
                .description = "0x220",
                .value = 0x220
            },
            {
                .description = "0x230",
                .value = 0x230
            },
            {
                .description = "0x240",
                .value = 0x240
            },
            {
                .description = "0x250",
                .value = 0x250
            },
            {
                .description = "0x260",
                .value = 0x260 },
            { .description = "" }
        }
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 7,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "IRQ 2",
                .value = 2
            },
            {
                .description = "IRQ 3",
                .value = 3
            },
            {
                .description = "IRQ 5",
                .value = 5
            },
            {
                .description = "IRQ 7",
                .value = 7
            },
            { .description = "" }
        }
    },
    {
        .name = "dma",
        .description = "DMA",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 1,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "DMA 1",
                .value = 1
            },
            {
                .description = "DMA 3",
                .value = 3
            },
            { "" }
        }
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
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t sb15_config[] = {
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x220,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "0x210",
                .value = 0x210
            },
            {
                .description = "0x220",
                .value = 0x220
            },
            {
                .description = "0x230",
                .value = 0x230
            },
            {
                .description = "0x240",
                .value = 0x240
            },
            {
                .description = "0x250",
                .value = 0x250
            },
            {
                .description = "0x260",
                .value = 0x260
            },
            {
                .description = "" }
        }
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 7,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "IRQ 2",
                .value = 2
            },
            {
                .description = "IRQ 3",
                .value = 3
            },
            {
                .description = "IRQ 5",
                .value = 5
            },
            {
                .description = "IRQ 7",
                .value = 7
            },
            { .description = "" }
        }
    },
    {
        .name = "dma",
        .description = "DMA",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 1,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "DMA 1",
                .value = 1
            },
            {
                .description = "DMA 3",
                .value = 3
            },
            { .description = "" }
        }
    },
    {
        .name = "opl",
        .description = "Enable OPL",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    {
        .name = "cms",
        .description = "Enable CMS",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
    },
    {
        .name = "receive_input",
        .description = "Receive input (SB MIDI)",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t sb2_config[] = {
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x220,
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
                .description = "0x260",
                .value = 0x260
            },
            { .description = "" }
        }
    },
    {
        .name = "mixaddr",
        .description = "Mixer",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "Disabled",
                .value = 0
            },
            {
                .description = "0x220",
                .value = 0x220
            },
            {
                .description = "0x240",
                .value = 0x240
            },
            {
                .description = "0x250",
                .value = 0x250
            },
            {
                .description = "0x260",
                .value = 0x260
            },
            { .description = "" }
        }
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 5,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "IRQ 2",
                .value = 2
            },
            {
                .description = "IRQ 3",
                .value = 3
            },
            {
                .description = "IRQ 5",
                .value = 5
            },
            {
                .description = "IRQ 7",
                .value = 7
            },
            { .description = "" }
        }
    },
    {
        .name = "dma",
        .description = "DMA",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 1,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "DMA 1",
                .value = 1
            },
            {
                .description = "DMA 3",
                .value = 3
            },
            { .description = "" }
        }
    },
    {
        .name = "opl",
        .description = "Enable OPL",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    {
        .name = "cms",
        .description = "Enable CMS",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
    },
    {
        .name = "receive_input",
        .description = "Receive input (SB MIDI)",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t sb_mcv_config[] = {
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 7,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "IRQ 2",
                .value = 2
            },
            {
                .description = "IRQ 3",
                .value = 3
            },
            {
                .description = "IRQ 5",
                .value = 5
            },
            {
                .description = "IRQ 7",
                .value = 7
            },
            { .description = "" }
        }
    },
    {
        .name = "dma",
        .description = "DMA",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 1,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "DMA 1",
                .value = 1
            },
            {
                .description = "DMA 3",
                .value = 3
            },
            { .description = "" }
        }
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
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t sb_pro_config[] = {
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x220,
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
            { .description = "" }
        }
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 7,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "IRQ 2",
                .value = 2
            },
            {
                .description = "IRQ 5",
                .value = 5
            },
            {
                .description = "IRQ 7",
                .value = 7
            },
            {
                .description = "IRQ 10",
                .value = 10
            },
            { .description = "" }
        }
    },
    {
        .name = "dma",
        .description = "DMA",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 1,
        .file_filter = "",
        .spinner = { 0 },
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
            { .description = "" }
        }
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
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t sb_pro_mcv_config[] = {
    {
        .name = "receive_input",
        .description = "Receive input (SB MIDI)",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t sb_16_config[] = {
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x220,
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
                .description = "0x260",
                .value = 0x260
            },
            {
                .description = "0x280",
                .value = 0x280
            },
            { .description = "" }
        }
    },
    {
        .name = "base401",
        .description = "MPU-401 Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x330,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "Disabled",
                .value = 0
            },
            {
                .description = "0x300",
                .value = 0x300
            },
            {
                .description = "0x330",
                .value = 0x330
            },
            { .description = "" }
        }
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 5,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "IRQ 2",
                .value = 2
            },
            {
                .description = "IRQ 5",
                .value = 5
            },
            {
                .description = "IRQ 7",
                .value = 7
            },
            {
                .description = "IRQ 10",
                .value = 10
            },
            { .description = "" }
        }
    },
    {
        .name = "dma",
        .description = "Low DMA channel",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 1,
        .file_filter = "",
        .spinner = { 0 },
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
            { .description = "" }
        }
    },
    {
        .name = "dma16",
        .description = "High DMA channel",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 5,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "DMA 5",
                .value = 5
            },
            {
                .description = "DMA 6",
                .value = 6
            },
            {
                .description = "DMA 7",
                .value = 7
            },
            { .description = "" }
        }
    },
    {
        .name = "opl",
        .description = "Enable OPL",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    {
        .name = "control_pc_speaker",
        .description = "Control PC speaker",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
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
};

static const device_config_t sb_16_pnp_config[] = {
    {
        .name = "control_pc_speaker",
        .description = "Control PC speaker",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
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
};

static const device_config_t sb_32_pnp_config[] = {
    {
        .name = "onboard_ram",
        .description = "Onboard RAM",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "None",
                .value = 0
            },
            {
                .description = "512 KB",
                .value = 512
            },
            {
                .description = "2 MB",
                .value = 2048
            },
            {
                .description = "8 MB",
                .value = 8192
            },
            {
                .description = "28 MB",
                .value = 28672
            },
            { .description = "" }
        }
    },
    {
        .name = "control_pc_speaker",
        .description = "Control PC speaker",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
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
};

static const device_config_t sb_awe32_config[] = {
    {
        .name = "base",
        .description = "Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x220,
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
                .description = "0x260",
                .value = 0x260
            },
            {
                .description = "0x280",
                .value = 0x280
            },
            { .description = "" }
        }
    },
    {
        .name = "emu_base",
        .description =  "EMU8000 Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x620,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "0x620",
                .value = 0x620
            },
            {
                .description = "0x640",
                .value = 0x640
            },
            {
                .description = "0x660",
                .value = 0x660
            },
            {
                .description = "0x680",
                .value = 0x680
            },
            { .description = ""}
        }
    },
    {
        .name = "base401",
        .description = "MPU-401 Address",
        .type = CONFIG_HEX16,
        .default_string = "",
        .default_int = 0x330,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "Disabled",
                .value = 0
            },
            {
                .description = "0x300",
                .value = 0x300
            },
            {
                .description = "0x330",
                .value = 0x330
            },
            { .description = "" }
        }
    },
    {
        .name = "irq",
        .description = "IRQ",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 5,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "IRQ 2",
                .value = 2
            },
            {
                .description = "IRQ 5",
                .value = 5
            },
            {
                .description = "IRQ 7",
                .value = 7
            },
            {
                .description = "IRQ 10",
                .value = 10
            },
            { .description = "" }
        }
    },
    {
        .name = "dma",
        .description = "Low DMA channel",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 1,
        .file_filter = "",
        .spinner = { 0 },
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
            { .description = "" }
        }
    },
    {
        .name = "dma16",
        .description = "High DMA channel",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 5,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "DMA 5",
                .value = 5
            },
            {
                .description = "DMA 6",
                .value = 6
            },
            {
                .description = "DMA 7",
                .value = 7
            },
            { .description = "" }
        }
    },
    {
        .name = "onboard_ram",
        .description = "Onboard RAM",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 512,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "None",
                .value = 0
            },
            {
                .description = "512 KB",
                .value = 512
            },
            {
                .description = "2 MB",
                .value = 2048
            },
            {
                .description = "8 MB",
                .value = 8192
            },
            {
                .description = "28 MB",
                .value = 28672
            },
            { "" }
        }
    },
    {
        .name = "opl",
        .description = "Enable OPL",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 1
    },
    {
        .name = "control_pc_speaker",
        .description = "Control PC speaker",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
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
};

static const device_config_t sb_awe32_pnp_config[] = {
    {
        .name = "onboard_ram",
        .description = "Onboard RAM",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 512,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "None",
                .value = 0
            },
            {
                .description = "512 KB",
                .value = 512
            },
            {
                .description = "2 MB",
                .value = 2048
            },
            {
                .description = "8 MB",
                .value = 8192
            },
            {
                .description = "28 MB",
                .value = 28672
            },
            { .description = "" }
        }
    },
    {
        .name = "control_pc_speaker",
        .description = "Control PC speaker",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
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
};

static const device_config_t sb_awe64_value_config[] = {
    {
        .name = "onboard_ram",
        .description = "Onboard RAM",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 512,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "512 KB",
                .value = 512
            },
            {
                .description = "1 MB",
                .value = 1024
            },
            {
                .description = "2 MB",
                .value = 2048
            },
            {
                .description = "4 MB",
                .value = 4096
            },
            {
                .description = "8 MB",
                .value = 8192
            },
            {
                .description = "12 MB",
                .value = 12288
            },
            {
                .description = "16 MB",
                .value = 16384
            },
            {
                .description = "20 MB",
                .value = 20480
            },
            {
                .description = "24 MB",
                .value = 24576
            },
            {
                .description = "28 MB",
                .value = 28672
            },
            { .description = "" }
        }
    },
    {
        .name = "control_pc_speaker",
        .description = "Control PC speaker",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
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
};

static const device_config_t sb_awe64_config[] = {
    {
        .name = "onboard_ram",
        .description = "Onboard RAM",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 1024,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "1 MB",
                .value = 1024
            },
            {
                .description = "2 MB",
                .value = 2048
            },
            {
                .description = "4 MB",
                .value = 4096
            },
            {
                .description = "8 MB",
                .value = 8192
            },
            {
                .description = "12 MB",
                .value = 12288
            },
            {
                .description = "16 MB",
                .value = 16384
            },
            {
                .description = "20 MB",
                .value = 20480
            },
            {
                .description = "24 MB",
                .value = 24576
            },
            {
                .description = "28 MB",
                .value = 28672
            },
            { .description = "" }
        }
    },
    {
        .name = "control_pc_speaker",
        .description = "Control PC speaker",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
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
};

static const device_config_t sb_awe64_gold_config[] = {
    {
        .name = "onboard_ram",
        .description = "Onboard RAM",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 4096,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            {
                .description = "4 MB",
                .value = 4096
            },
            {
                .description = "8 MB",
                .value = 8192
            },
            {
                .description = "12 MB",
                .value = 12288
            },
            {
                .description = "16 MB",
                .value = 16384
            },
            {
                .description = "20 MB",
                .value = 20480
            },
            {
                .description = "24 MB",
                .value = 24576
            },
            {
                .description = "28 MB",
                .value = 28672
            },
            { .description = "" }
        }
    },
    {
        .name = "control_pc_speaker",
        .description = "Control PC speaker",
        .type = CONFIG_BINARY,
        .default_string = "",
        .default_int = 0
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
};

static const device_config_t ess_1688_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = "",
        .default_int    = 0x220,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            {
                .description = "0x220",
                .value       = 0x220
            },
            {
                .description = "0x230",
                .value       = 0x230
            },
            {
                .description = "0x240",
                .value       = 0x240
            },
            {
                .description = "0x250",
                .value       = 0x250
            },
            { .description = "" }
        }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 5,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            {
                .description = "IRQ 2",
                .value       = 2
            },
            {
                .description = "IRQ 5",
                .value       = 5
            },
            {
                .description = "IRQ 7",
                .value       = 7
            },
            {
                .description = "IRQ 10",
                .value       = 10
            },
            { .description = "" }
        }
    },
    {
        .name           = "dma",
        .description    = "DMA",
        .type           = CONFIG_SELECTION,
        .default_string = "",
        .default_int    = 1,
        .file_filter    = "",
        .spinner        = { 0 },
        .selection      = {
            {
                .description = "DMA 0",
                .value       = 0
            },
            {
                .description = "DMA 1",
                .value       = 1
            },
            {
                .description = "DMA 3",
                .value       = 3
            },
            { .description = "" }
        }
    },
    {
        .name           = "opl",
        .description    = "Enable OPL",
        .type           = CONFIG_BINARY,
        .default_string = "",
        .default_int    = 1
    },
    {
        .name           = "receive_input",
        .description    = "Receive input (SB MIDI)",
        .type           = CONFIG_BINARY,
        .default_string = "",
        .default_int    = 1
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t sb_1_device = {
    .name          = "Sound Blaster v1.0",
    .internal_name = "sb",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = sb_1_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_config
};

const device_t sb_15_device = {
    .name          = "Sound Blaster v1.5",
    .internal_name = "sb1.5",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = sb_15_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb15_config
};

const device_t sb_mcv_device = {
    .name          = "Sound Blaster MCV",
    .internal_name = "sbmcv",
    .flags         = DEVICE_MCA,
    .local         = 0,
    .init          = sb_mcv_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_mcv_config
};

const device_t sb_2_device = {
    .name          = "Sound Blaster v2.0",
    .internal_name = "sb2.0",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = sb_2_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb2_config
};

const device_t sb_pro_v1_device = {
    .name          = "Sound Blaster Pro v1",
    .internal_name = "sbprov1",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = sb_pro_v1_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_pro_config
};

const device_t sb_pro_v2_device = {
    .name          = "Sound Blaster Pro v2",
    .internal_name = "sbprov2",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = sb_pro_v2_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_pro_config
};

const device_t sb_pro_mcv_device = {
    .name          = "Sound Blaster Pro MCV",
    .internal_name = "sbpromcv",
    .flags         = DEVICE_MCA,
    .local         = 0,
    .init          = sb_pro_mcv_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_pro_mcv_config
};

const device_t sb_pro_compat_device = {
    .name          = "Sound Blaster Pro (Compatibility)",
    .internal_name = "sbpro_compat",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = sb_pro_compat_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sb_16_device = {
    .name          = "Sound Blaster 16",
    .internal_name = "sb16",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = FM_YMF262,
    .init          = sb_16_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_16_config
};

const device_t sb_vibra16s_onboard_device = {
    .name          = "Sound Blaster ViBRA 16S (On-Board)",
    .internal_name = "sb_vibra16s_onboard",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = FM_YMF289B,
    .init          = sb_16_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_16_config
};

const device_t sb_vibra16s_device = {
    .name          = "Sound Blaster ViBRA 16S",
    .internal_name = "sb_vibra16s",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = FM_YMF289B,
    .init          = sb_16_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_16_config
};

const device_t sb_vibra16xv_device = {
    .name          = "Sound Blaster ViBRA 16XV",
    .internal_name = "sb_vibra16xv",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = sb_vibra16_pnp_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = sb_vibra16xv_available },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_16_pnp_config
};

const device_t sb_vibra16c_onboard_device = {
    .name          = "Sound Blaster ViBRA 16C (On-Board)",
    .internal_name = "sb_vibra16c_onboard",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 1,
    .init          = sb_vibra16_pnp_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = sb_vibra16c_available },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_16_pnp_config
};

const device_t sb_vibra16c_device = {
    .name          = "Sound Blaster ViBRA 16C",
    .internal_name = "sb_vibra16c",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 1,
    .init          = sb_vibra16_pnp_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = sb_vibra16c_available },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_16_pnp_config
};

const device_t sb_16_reply_mca_device = {
    .name          = "Sound Blaster 16 Reply MCA",
    .internal_name = "sb16_reply_mca",
    .flags         = DEVICE_MCA,
    .local         = 0,
    .init          = sb_16_reply_mca_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_16_pnp_config
};

const device_t sb_16_pnp_device = {
    .name          = "Sound Blaster 16 PnP",
    .internal_name = "sb16_pnp",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = sb_16_pnp_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_16_pnp_config
};

const device_t sb_16_compat_device = {
    .name          = "Sound Blaster 16 (Compatibility)",
    .internal_name = "sb16_compat",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 1,
    .init          = sb_16_compat_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sb_16_compat_nompu_device = {
    .name          = "Sound Blaster 16 (Compatibility - MPU-401 Off)",
    .internal_name = "sb16_compat",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = sb_16_compat_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sb_32_pnp_device = {
    .name          = "Sound Blaster 32 PnP",
    .internal_name = "sb32_pnp",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = sb_awe32_pnp_init,
    .close         = sb_awe32_close,
    .reset         = NULL,
    { .available = sb_32_pnp_available },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_32_pnp_config
};

const device_t sb_awe32_device = {
    .name          = "Sound Blaster AWE32",
    .internal_name = "sbawe32",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = sb_awe32_init,
    .close         = sb_awe32_close,
    .reset         = NULL,
    { .available = sb_awe32_available },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_awe32_config
};

const device_t sb_awe32_pnp_device = {
    .name          = "Sound Blaster AWE32 PnP",
    .internal_name = "sbawe32_pnp",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 1,
    .init          = sb_awe32_pnp_init,
    .close         = sb_awe32_close,
    .reset         = NULL,
    { .available = sb_awe32_pnp_available },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_awe32_pnp_config
};

const device_t sb_awe64_value_device = {
    .name          = "Sound Blaster AWE64 Value",
    .internal_name = "sbawe64_value",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 2,
    .init          = sb_awe32_pnp_init,
    .close         = sb_awe32_close,
    .reset         = NULL,
    { .available = sb_awe64_value_available },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_awe64_value_config
};

const device_t sb_awe64_device = {
    .name          = "Sound Blaster AWE64",
    .internal_name = "sbawe64",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 3,
    .init          = sb_awe32_pnp_init,
    .close         = sb_awe32_close,
    .reset         = NULL,
    { .available = sb_awe64_available },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_awe64_config
};

const device_t sb_awe64_gold_device = {
    .name          = "Sound Blaster AWE64 Gold",
    .internal_name = "sbawe64_gold",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 4,
    .init          = sb_awe32_pnp_init,
    .close         = sb_awe32_close,
    .reset         = NULL,
    { .available = sb_awe64_gold_available },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_awe64_gold_config
};

const device_t ess_1688_device = {
    .name          = "ESS AudioDrive ES1688",
    .internal_name = "ess_es1688",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = ess_1688_init,
    .close         = sb_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = sb_speed_changed,
    .force_redraw  = NULL,
    .config        = ess_1688_config
};
