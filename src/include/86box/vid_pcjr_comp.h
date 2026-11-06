/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM PCjr composite artifact colour emulation, ported from
 *          DOSBox Staging.
 *
 * Authors: reenigne, NewRisingSun, DOSBox Staging Team
 *
 *          Copyright 2002-2021 DOSBox Team.
 *          Copyright 2020-2026 DOSBox Staging Team.
 */
#ifndef VIDEO_PCJR_COMP_H
#define VIDEO_PCJR_COMP_H

/* Fill lut[] (256 entries, 0x00RRGGBB) with the artifact colours of the
   PCjr's 320x200x4 (bpp1 = 0) or 640x200x2 (bpp1 = 1) graphics modes.
   colors[] are the four RGBI palette entries; only 160 slots are used,
   the rest are cleared. brightness, contrast and saturation are in the
   units of the CGA composite settings (0, 100 and 100 by default). */
void pcjr_comp_generate(uint32_t *lut, int new_era, int bw, int bpp1, int hue,
                        int brightness, int contrast, int saturation,
                        const uint8_t *colors);

/* Decode one line of raw video memory (len bytes, two bits per hdot pair,
   MSB first) into len * 8 hdots of RGB. */
void pcjr_comp_line(const uint32_t *lut, const uint8_t *raw, int len, uint32_t *out);

#endif /*VIDEO_PCJR_COMP_H*/
