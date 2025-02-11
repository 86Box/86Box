/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           Pro Audio Spectrum Plus and 16 emulation.
 *
 *           Original PAS uses:
 *               - 2 x OPL2;
 *               - PIT - sample rate/count;
 *               - LMC835N/LMC1982 - mixer;
 *               - YM3802 - MIDI Control System.
 *
 *           9A01 - I/O base:
 *               - base >> 2.
 *
 *           All below + I/O base.
 *
 *           B89 - Interrupt status / clear:
 *               - Bit 2 - sample rate;
 *               - Bit 3 - PCM;
 *               - Bit 4 - MIDI.
 *
 *           B88 - Audio mixer control register.
 *
 *           B8A - Audio filter control:
 *               - Bit 5 - mute?.
 *
 *           B8B - Interrupt mask / board ID:
 *               - Bits 5-7 - board ID (read only on PAS16).
 *
 *           F88 - PCM data (low).
 *
 *           F89 - PCM data (high).
 *
 *           F8A - PCM control?:
 *               - Bit 4 - input/output select (1 = output);
 *               - Bit 5 - mono/stereo select;
 *               - Bit 6 - PCM enable.
 *
 *           1388-138B - PIT clocked at 1193180 Hz:
 *               - 1388 - Sample rate;
 *               - 1389 - Sample count.
 *
 *           178B - ????.
 *           2789 - Board revision.
 *
 *           8389:
 *               - Bit 2 - 8/16 bit.
 *
 *           BF88 - Wait states.
 *
 *           EF8B:
 *               - Bit 3 - 16 bits okay ?.
 *
 *           F388:
 *               - Bit 6 - joystick enable.
 *
 *           F389:
 *               - Bits 0-2 - DMA.
 *
 *           F38A:
 *               - Bits 0-3 - IRQ.
 *
 *           F788:
 *               - Bit 1 - SB emulation;
 *               - Bit 0 - MPU401 emulation.
 *
 *           F789 - SB base address:
 *               - Bits 0-3 - Address bits 4-7.
 *
 *           FB8A - SB IRQ/DMA:
 *               - Bits 3-5 - IRQ;
 *               - Bits 6-7 - DMA.
 *
 *           FF88 - board model:
 *               - 3 = PAS16.
 *
 * Authors:  Sarah Walker, <https://pcem-emulator.co.uk/>
 *           Miran Grca, <mgrca8@gmail.com>
 *           TheCollector1995, <mariogplayer@gmail.com>
 *
 *           Copyright 2008-2024 Sarah Walker.
 *           Copyright 2024 Miran Grca.
 */
#define _USE_MATH_DEFINES
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H

#include "cpu.h"
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/filters.h>
#include <86box/plat_unused.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/midi.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/pit_fast.h>
#include <86box/rom.h>
#include <86box/scsi_device.h>
#include <86box/scsi_ncr5380.h>
#include <86box/scsi_t128.h>
#include <86box/snd_mpu401.h>
#include <86box/sound.h>
#include <86box/snd_opl.h>
#include <86box/snd_sb.h>
#include <86box/snd_sb_dsp.h>

typedef struct nsc_mixer_t {
    double master_l;
    double master_r;

    int bass;
    int treble;

    double fm_l;
    double fm_r;
    double imixer_l;
    double imixer_r;
    double line_l;
    double line_r;
    double cd_l;
    double cd_r;
    double mic_l;
    double mic_r;
    double pcm_l;
    double pcm_r;
    double speaker_l;
    double speaker_r;

    uint8_t lmc1982_regs[8];
    uint8_t lmc835_regs[32];

    uint8_t  im_state;
    uint16_t im_data[4];
} nsc_mixer_t;

typedef struct mv508_mixer_t {
    double master_l;
    double master_r;

    int bass;
    int treble;

    double fm_l;
    double fm_r;
    double imixer_l;
    double imixer_r;
    double line_l;
    double line_r;
    double cd_l;
    double cd_r;
    double mic_l;
    double mic_r;
    double pcm_l;
    double pcm_r;
    double speaker_l;
    double speaker_r;
    double sb_l;
    double sb_r;

    uint8_t index;
    uint8_t regs[3][128];
} mv508_mixer_t;

typedef struct pas16_t {
    uint8_t  this_id;
    uint8_t  board_id;
    uint8_t  master_ff;
    uint8_t  has_scsi;
    uint8_t  dma;
    uint8_t  sb_irqdma;
    uint8_t  type;
    uint8_t  filter;

    uint8_t  audiofilt;
    uint8_t  audio_mixer;
    uint8_t  compat;
    uint8_t  compat_base;
    uint8_t  io_conf_1;
    uint8_t  io_conf_2;
    uint8_t  io_conf_3;
    uint8_t  io_conf_4;

    uint8_t  irq_stat;
    uint8_t  irq_ena;
    uint8_t  pcm_ctrl;
    uint8_t  prescale_div;
    uint8_t  stereo_lr;
    uint8_t  dma8_ff;
    uint8_t  waitstates;
    uint8_t  enhancedscsi;

    uint8_t  sys_conf_1;
    uint8_t  sys_conf_2;
    uint8_t  sys_conf_3;
    uint8_t  sys_conf_4;
    uint8_t  midi_ctrl;
    uint8_t  midi_stat;
    uint8_t  midi_data;
    uint8_t  fifo_stat;

    uint8_t  timeout_count;
    uint8_t  timeout_status;
    uint8_t  pad_array[6];

    uint8_t  midi_queue[256];

    uint16_t base;
    uint16_t new_base;
    uint16_t sb_compat_base;
    uint16_t mpu401_base;
    uint16_t dma8_dat;
    uint16_t ticks;
    uint16_t pcm_dat_l;
    uint16_t pcm_dat_r;

    int32_t  pcm_buffer[2][SOUNDBUFLEN * 2];

    int      pos;
    int      midi_r;
    int      midi_w;
    int      midi_uart_out;
    int      midi_uart_in;
    int      sysex;

    int      irq;
    int      scsi_irq;

    nsc_mixer_t   nsc_mixer;
    mv508_mixer_t mv508_mixer;

    fm_drv_t opl;
    sb_dsp_t dsp;

    mpu_t *  mpu;

    pitf_t * pit;

    t128_t * scsi;

    pc_timer_t scsi_timer;
} pas16_t;

static uint8_t pas16_next = 0;

static void    pas16_update(pas16_t *pas16);

static int pas16_dmas[8]    = { 4, 1, 2, 3, 0, 5, 6, 7 };
static int pas16_sb_irqs[8] = { 0, 2, 3, 5, 7, 10, 11, 12 };
static int pas16_sb_dmas[8] = { 0, 1, 2, 3 };

enum {
    PAS16_INT_SAMP = 0x04,
    PAS16_INT_PCM  = 0x08,
    PAS16_INT_MIDI = 0x10
};

enum {
    PAS16_PCM_MONO = 0x20,
    PAS16_PCM_ENA  = 0x40,
    PAS16_PCM_DMA_ENA = 0x80
};

enum {
    PAS16_SC2_16BIT  = 0x04,
    PAS16_SC2_MSBINV = 0x10
};

enum {
    PAS16_FILT_MUTE = 0x20
};

enum {
    STATE_SM_IDLE = 0x00,
    STATE_LMC1982_ADDR   = 0x10,
    STATE_LMC1982_ADDR_0 = 0x10,
    STATE_LMC1982_ADDR_1,
    STATE_LMC1982_ADDR_2,
    STATE_LMC1982_ADDR_3,
    STATE_LMC1982_ADDR_4,
    STATE_LMC1982_ADDR_5,
    STATE_LMC1982_ADDR_6,
    STATE_LMC1982_ADDR_7,
    STATE_LMC1982_ADDR_OVER,
    STATE_LMC1982_DATA   = 0x10,
    STATE_LMC1982_DATA_0 = 0x20,
    STATE_LMC1982_DATA_1,
    STATE_LMC1982_DATA_2,
    STATE_LMC1982_DATA_3,
    STATE_LMC1982_DATA_4,
    STATE_LMC1982_DATA_5,
    STATE_LMC1982_DATA_6,
    STATE_LMC1982_DATA_7,
    STATE_LMC1982_DATA_8,
    STATE_LMC1982_DATA_9,
    STATE_LMC1982_DATA_A,
    STATE_LMC1982_DATA_B,
    STATE_LMC1982_DATA_C,
    STATE_LMC1982_DATA_D,
    STATE_LMC1982_DATA_E,
    STATE_LMC1982_DATA_F,
    STATE_LMC1982_DATA_OVER,
    STATE_LMC835_DATA   = 0x40,
    STATE_LMC835_DATA_0 = 0x40,
    STATE_LMC835_DATA_1,
    STATE_LMC835_DATA_2,
    STATE_LMC835_DATA_3,
    STATE_LMC835_DATA_4,
    STATE_LMC835_DATA_5,
    STATE_LMC835_DATA_6,
    STATE_LMC835_DATA_7,
    STATE_LMC835_DATA_OVER,
};

#define    STATE_DATA_MASK           0x07
#define    STATE_DATA_MASK_W         0x0f

#define    LMC1982_ADDR              0x00
#define    LMC1982_DATA              0x01
#define    LMC835_ADDR               0x02
#define    LMC835_DATA               0x03

#define    LMC835_FLAG_ADDR          0x80

#define    SERIAL_MIXER_IDENT        0x10
#define    SERIAL_MIXER_STROBE       0x04
#define    SERIAL_MIXER_CLOCK        0x02
#define    SERIAL_MIXER_DATA         0x01
#define    SERIAL_MIXER_RESET_PCM    0x01

#define PAS16_PCM_AND_DMA_ENA (PAS16_PCM_ENA | PAS16_PCM_DMA_ENA)

/*
   LMC1982CIN registers (data bits 7 and 6 are always don't care):
   - 40              = Mode (0x01 = INPUT2);
   - 41              = 0 = Loudness (1 = on, 0 = off), 1 = Stereo Enhance (1 = on, 0 = off)
                       (0x00);
   - 42              = Bass, 00-0c (0x06);
   - 43              = Treble, 00-0c (0x06);
   - 45 [L] / 44 [R] = Master, 28-00, counting down (0x28, later 0xa8);
   - 46              = ???? (0x05 = Stereo).
 */
#define LMC1982_REG_MASK      0xf8    /* LMC1982CIN: Register Mask */
#define LMC1982_REG_VALID     0x40    /* LMC1982CIN: Register Valid Value */

#define LMC1982_REG_ISELECT   0x00    /* LMC1982CIN: Input Select + Mute */
#define LMC1982_REG_LES       0x01    /* LMC1982CIN: Loudness, Enhanced Stereo */
#define LMC1982_REG_BASS      0x02    /* LMC1982CIN: Bass */
#define LMC1982_REG_TREBLE    0x03    /* LMC1982CIN: Treble */
/* The Windows 95 driver indicates left and right are swapped in the wiring. */
#define LMC1982_REG_VOL_L     0x04    /* LMC1982CIN: Left Volume */
#define LMC1982_REG_VOL_R     0x05    /* LMC1982CIN: Right Volume */
#define LMC1982_REG_MODE      0x06    /* LMC1982CIN: Mode */
#define LMC1982_REG_DINPUT    0x07    /* LMC1982CIN: Digital Input 1 and 2 */

/* Bits 7-2: Don't care. */
#define LMC1982_ISELECT_MASK  0x03    /* LMC1982CIN: Input Select: Mask */
#define LMC1982_ISELECT_I1    0x00    /* LMC1982CIN: Input Select: INPUT1 */
#define LMC1982_ISELECT_I2    0x01    /* LMC1982CIN: Input Select: INPUT2 */
#define LMC1982_ISELECT_NA    0x02    /* LMC1982CIN: Input Select: N/A */
#define LMC1982_ISELECT_MUTE  0x03    /* LMC1982CIN: Input Select: MUTE */

/* Bits 7-2: Don't care. */
#define LMC1982_LES_MASK      0x03    /* LMC1982CIN: Loudness, Enhanced Stereo: Mask */
#define LMC1982_LES_BOTH_OFF  0x00    /* LMC1982CIN: Loudness OFF, Enhanced Stereo OFF */
#define LMC1982_L_ON_ES_OFF   0x01    /* LMC1982CIN: Loudness ON, Enhanced Stereo OFF */
#define LMC1982_L_OFF_ES_ON   0x02    /* LMC1982CIN: Loudness OFF, Enhanced Stereo ON */
#define LMC1982_LES_BOTH_ON   0x03    /* LMC1982CIN: Loudness OFF, Enhanced Stereo ON */

/* Bits 7-3: Don't care. */
#define LMC1982_MODE_MASK     0x07    /* LMC1982CIN: Mode: Mask */
#define LMC1982_MODE_MONO_L   0x04    /* LMC1982CIN: Mode: Left Mono */
#define LMC1982_MODE_STEREO   0x05    /* LMC1982CIN: Mode: Stereo */
#define LMC1982_MODE_MONO_R   0x06    /* LMC1982CIN: Mode: Right Mono */
#define LMC1982_MODE_MONO_R_2 0x07    /* LMC1982CIN: Mode: Right Mono (Alternate value) */

/* Bits 7-2: Don't care. */
#define LMC1982_DINPUT_MASK   0x03    /* LMC1982CIN: Digital Input 1 and 2: Mask */
#define LMC1982_DINPUT_DI1    0x01    /* LMC1982CIN: Digital Input 1 */
#define LMC1982_DINPUT_DI2    0x02    /* LMC1982CIN: Digital Input 2 */

/*
   LMC835N registers:
   - 01 [L] / 08 [R] = FM, 00-2f, bit 6 = clear, but the DOS driver sets
                       the bit, indicating a volume boost (0x69);
   - 02 [L] / 09 [R] = Rec. monitor, 00-2f, bit 6 = clear (0x29);
   - 03 [L] / 0A [R] = Line in, 00-2f, bit 6 = clear, except for the DOS
                       driver (0x69);
   - 04 [L] / 0B [R] = CD, 00-2f, bit 6 = clear, except for the DOS driver
                       (0x69);
   - 05 [L] / 0C [R] = Microphone, 00-2f, bit 6 = clear, except for the DOS
                       driver (0x44);
   - 06 [L] / 0D [R] = Wave out, 00-2f, bit 6 = set, except for DOS driver,
                       which clears the bit (0x29);
   - 07 [L] / 0E [R] = PC speaker, 00-2f, bit 6 = clear, except for the DOS
                       drive (0x06).
   The registers for which the DOS driver sets the boost, also have bit 6
   set in the address, despite the fact it should be a don't care - why?
   Apparently, no Sound Blaster control.
*/
#define LMC835_REG_MODE       0x00    /* LMC835N: Mode, not an actual register, but our internal register
                                                  to store the mode for Channels A (1-7) and B (8-e). */

#define LMC835_REG_FM_L       0x01    /* LMC835N: FM Left */
#define LMC835_REG_IMIXER_L   0x02    /* LMC835N: Record Monitor Left */
#define LMC835_REG_LINE_L     0x03    /* LMC835N: Line in Left */
#define LMC835_REG_CDROM_L    0x04    /* LMC835N: CD Left */
#define LMC835_REG_MIC_L      0x05    /* LMC835N: Microphone Left */
#define LMC835_REG_PCM_L      0x06    /* LMC835N: Wave out Left */
#define LMC835_REG_SPEAKER_L  0x07    /* LMC83N5: PC speaker Left */

#define LMC835_REG_FM_R       0x08    /* LMC835N: FM Right */
#define LMC835_REG_IMIXER_R   0x09    /* LMC835N: Record Monitor Right */
#define LMC835_REG_LINE_R     0x0a    /* LMC835N: Line in Right */
#define LMC835_REG_CDROM_R    0x0b    /* LMC835N: CD Right */
#define LMC835_REG_MIC_R      0x0c    /* LMC835N: Microphone Right */
#define LMC835_REG_PCM_R      0x0d    /* LMC835N: Wave out Right */
#define LMC835_REG_SPEAKER_R  0x0e    /* LMC83N5: PC speaker Right */

#define MV508_ADDRESS    0x80    /* Flag indicating it is the address */
/*
   I think this may actually operate as such:
       - Bit 6: Mask left channel;
       - Bit 5: Mask right channel.
 */
#define MV508_CHANNEL    0x60
#define MV508_LEFT       0x20
#define MV508_RIGHT      0x40
#define MV508_BOTH       0x00
#define MV508_MIXER      0x10    /* Flag indicating it is a mixer rather than a volume */

#define MV508_INPUT_MIX  0x20    /* Flag indicating the selected mixer is input */

#define MV508_MASTER_A   0x01    /* Volume: Output */
#define MV508_MASTER_B   0x02    /* Volume: DSP input */
#define MV508_BASS       0x03    /* Volume: Bass */
#define MV508_TREBLE     0x04    /* Volume: Treble */
#define MV508_MODE       0x05    /* Volume: Mode */

#define MV508_LOUDNESS   0x04    /* Mode: Loudness */
#define MV508_ENH_MASK   0x03    /* Mode: Stereo enhancement bit mask */
#define MV508_ENH_NONE   0x00    /* Mode: No stereo enhancement */
#define MV508_ENH_40     0x01    /* Mode: 40% stereo enhancement */
#define MV508_ENH_60     0x02    /* Mode: 60% stereo enhancement */
#define MV508_ENH_80     0x03    /* Mode: 80% stereo enhancement */

#define MV508_FM         0x00    /* Mixer: FM */
#define MV508_IMIXER     0x01    /* Mixer: Input mixer (recording monitor) */
#define MV508_LINE       0x02    /* Mixer: Line in */
#define MV508_CDROM      0x03    /* Mixer: CD-ROM */
#define MV508_MIC        0x04    /* Mixer: Microphone */
#define MV508_PCM        0x05    /* Mixer: PCM */
#define MV508_SPEAKER    0x06    /* Mixer: PC Speaker */
#define MV508_SB         0x07    /* Mixer: Sound Blaster DSP */

#define MV508_REG_MASTER_A_L    (MV508_MASTER_A | MV508_LEFT)
#define MV508_REG_MASTER_A_R    (MV508_MASTER_A | MV508_RIGHT)
#define MV508_REG_MASTER_B_L    (MV508_MASTER_B | MV508_LEFT)
#define MV508_REG_MASTER_B_R    (MV508_MASTER_B | MV508_RIGHT)
#define MV508_REG_BASS          (MV508_BASS | MV508_LEFT)
#define MV508_REG_TREBLE        (MV508_TREBLE | MV508_LEFT)

#define MV508_REG_FM_L          (MV508_MIXER | MV508_FM | MV508_LEFT)
#define MV508_REG_FM_R          (MV508_MIXER | MV508_FM | MV508_RIGHT)
#define MV508_REG_IMIXER_L      (MV508_MIXER | MV508_IMIXER | MV508_LEFT)
#define MV508_REG_IMIXER_R      (MV508_MIXER | MV508_IMIXER | MV508_RIGHT)
#define MV508_REG_LINE_L        (MV508_MIXER | MV508_LINE | MV508_LEFT)
#define MV508_REG_LINE_R        (MV508_MIXER | MV508_LINE | MV508_RIGHT)
#define MV508_REG_CDROM_L       (MV508_MIXER | MV508_CDROM | MV508_LEFT)
#define MV508_REG_CDROM_R       (MV508_MIXER | MV508_CDROM | MV508_RIGHT)
#define MV508_REG_MIC_L         (MV508_MIXER | MV508_MIC | MV508_LEFT)
#define MV508_REG_MIC_R         (MV508_MIXER | MV508_MIC | MV508_RIGHT)
#define MV508_REG_PCM_L         (MV508_MIXER | MV508_PCM | MV508_LEFT)
#define MV508_REG_PCM_R         (MV508_MIXER | MV508_PCM | MV508_RIGHT)
#define MV508_REG_SPEAKER_L     (MV508_MIXER | MV508_SPEAKER | MV508_LEFT)
#define MV508_REG_SPEAKER_R     (MV508_MIXER | MV508_SPEAKER | MV508_RIGHT)
#define MV508_REG_SB_L          (MV508_MIXER | MV508_SB | MV508_LEFT)
#define MV508_REG_SB_R          (MV508_MIXER | MV508_SB | MV508_RIGHT)

double low_fir_pas16_coef[SB16_NCoef];

/*
   Also used for the MVA508.
 */
static double lmc1982_bass_treble_4bits[16];

/*
   Copied from the Sound Blaster code: -62 dB to 0 dB in 2 dB steps.
   Note that these are voltage dB's, so it corresonds in power dB
   (formula for conversion to percentage: 10 ^ (dB / 10)) to -31 dB
   to 0 dB in 1 dB steps.

   This is used for the MVA508 Volumes.
 */
static const double mva508_att_2dbstep_5bits[] = {
       25.0,    32.0,    41.0,    51.0,    65.0,    82.0,   103.0,   130.0,
      164.0,   206.0,   260.0,   327.0,   412.0,   519.0,   653.0,   822.0,
     1036.0,  1304.0,  1641.0,  2067.0,  2602.0,  3276.0,  4125.0,  5192.0,
     6537.0,  8230.0, 10362.0, 13044.0, 16422.0, 20674.0, 26027.0, 32767.0
};

/*
   The same but in 1 dB steps, used for the MVA508 Master Volume.
 */
static const double mva508_att_1dbstep_6bits[] = {
       18.0,    25.0,    29.0,    32.0,    36.0,    41.0,    46.0,    51.0,
       58.0,    65.0,    73.0,    82.0,    92.0,   103.0,   116.0,   130.0,
      146.0,   164.0,   184.0,   206.0,   231.0,   260.0,   292.0,   327.0,
      367.0,   412.0,   462.0,   519.0,   582.0,   653.0,   733.0,   822.0,
      923.0,  1036.0,  1162.0,  1304.0,  1463.0,  1641.0,  1842.0,  2067.0,
     2319.0,  2602.0,  2920.0,  3276.0,  3676.0,  4125.0,  4628.0,  5192.0,
     5826.0,  6537.0,  7335.0,  8230.0,  9234.0, 10362.0, 11626.0, 13044.0,
    14636.0, 16422.0, 18426.0, 20674.0, 23197.0, 26027.0, 29204.0, 32767.0
};

/*
   In 2 dB steps again, but to -80 dB and counting down (0 = Flat), used
   for the LMC1982CIN Master Volume.
 */
static const double lmc1982_att_2dbstep_6bits[] = {
    32767.0, 26027.0, 20674.0, 16422.0, 13044.0, 10362.0,  8230.0,  6537.0,
     5192.0,  4125.0,  3276.0,  2602.0,  2067.0,  1641.0,  1304.0,  1036.0,
      822.0,   653.0,   519.0,   412.0,   327.0,   260.0,   206.0,   164.0,
      130.0,   103.0,    82.0,    65.0,    51.0,    41.0,    32.0,    25.0,
       20.0,    16.0,    13.0,    10.0,     8.0,     6.0,     5.0,     4.0,
        3.0,     3.0,     3.0,     3.0,     3.0,     3.0,     3.0,     3.0,
        3.0,     3.0,     3.0,     3.0,     3.0,     3.0,     3.0,     3.0,
        3.0,     3.0,     3.0,     3.0,     3.0,     3.0,     3.0,     3.0
};

/*
    LMC385N attenuation, both +/- 12 dB and +/- 6 dB.

    Since the DOS and Windows 95 driver diverge on boost vs. cut for
    the various inputs, I think it is best to just do a 12 dB cut on
    the input, and then apply cut or boost as needed. I have factored
    in said cut in the below values.
 */
static const double lmc835_att_1dbstep_7bits[128] = {
    /* Flat */
    [0x40] =  8230.0,    /*       Flat  */
    /* Boost */
    [0x60] =  9234.0,    /*  1 dB Boost */
    [0x50] = 10362.0,    /*  2 dB Boost */
    [0x48] = 11626.0,    /*  3 dB Boost */
    [0x44] = 13044.0,    /*  4 dB Boost */
    [0x42] = 14636.0,    /*  5 dB Boost */
    [0x52] = 16422.0,    /*  6 dB Boost */
    [0x6a] = 18426.0,    /*  7 dB Boost */
    [0x56] = 20674.0,    /*  8 dB Boost */
    [0x41] = 23197.0,    /*  9 dB Boost */
    [0x69] = 26027.0,    /* 10 dB Boost */
    [0x6d] = 29204.0,    /* 11 dB Boost */
    /* The Win95 drivers use D5-D0 = 1D instead of 2D, datasheet erratum? */
    [0x5d] = 29204.0,    /* 11 dB Boost */
    [0x6f] = 32767.0,    /* 12 dB Boost */
    /* Flat */
    /* The datasheet says this should be Flat (1.0) but the
       Windows 95 drivers use this as basically mute (12 dB Cut). */
    [0x00] =  2067.0,    /* 12 dB Cut   */
    /* Cut - D5-D0 = 2F is minimum cut (0 dB) according to Windows 95 */
    [0x20] =  2319.0,    /* 11 dB Cut   */
    [0x10] =  2602.0,    /* 10 dB Cut   */
    [0x08] =  2920.0,    /*  9 dB Cut   */
    [0x04] =  3276.0,    /*  8 dB Cut   */
    [0x02] =  3676.0,    /*  7 dB Cut   */
    [0x12] =  4125.0,    /*  6 dB Cut   */
    [0x2a] =  4628.0,    /*  5 dB Cut   */
    [0x16] =  5192.0,    /*  4 dB Cut   */
    [0x01] =  5826.0,    /*  3 dB Cut   */
    [0x29] =  6537.0,    /*  2 dB Cut   */
    [0x2d] =  7335.0,    /*  1 dB Cut   */
    /* The Win95 drivers use D5-D0 = 1D instead of 2D, datasheet erratum? */
    [0x1d] =  7335.0,    /*  1 dB Cut   */
    [0x2f] =  8230.0,    /*       Flat  */
                 0.0
};

static const double lmc835_att_05dbstep_7bits[128] = {
    /* Flat */
    [0x40] =  8230.0,    /*        Flat  */
    /* Boost */
    [0x60] =  8718.0,    /* 0.5 dB Boost */
    [0x50] =  9234.0,    /* 1.0 dB Boost */
    [0x48] =  9782.0,    /* 1.5 dB Boost */
    [0x44] = 10362.0,    /* 2.0 dB Boost */
    [0x42] = 10975.0,    /* 2.5 dB Boost */
    [0x52] = 11626.0,    /* 3.0 dB Boost */
    [0x6a] = 12315.0,    /* 3.5 dB Boost */
    [0x56] = 13044.0,    /* 4.0 dB Boost */
    [0x41] = 13817.0,    /* 4.5 dB Boost */
    [0x69] = 14636.0,    /* 5.0 dB Boost */
    [0x6d] = 15503.0,    /* 5.5 dB Boost */
    /* The Win95 drivers use D5-D0 = 1D instead of 2D, datasheet erratum? */
    [0x5d] = 15503.0,    /* 5.5 dB Boost */
    [0x6f] = 16422.0,    /* 6.0 dB Boost */
    /* Flat */
    /* The datasheet says this should be Flat (1.0) but the
       Windows 95 drivers use this as basically mute (12 dB Cut). */
    [0x00] =  4125.0,    /* 6.0 dB Cut   */
    /* Cut - D5-D0 = 2F is minimum cut (0 dB) according to Windows 95 */
    [0x20] =  4369.0,    /* 5.5 dB Cut   */
    [0x10] =  4628.0,    /* 5.0 dB Cut   */
    [0x08] =  4920.0,    /* 4.5 dB Cut   */
    [0x04] =  5192.0,    /* 4.0 dB Cut   */
    [0x02] =  5500.0,    /* 3.5 dB Cut   */
    [0x12] =  5826.0,    /* 3.0 dB Cut   */
    [0x2a] =  6172.0,    /* 2.5 dB Cut   */
    [0x16] =  6537.0,    /* 2.0 dB Cut   */
    [0x01] =  6925.0,    /* 1.5 dB Cut   */
    [0x29] =  7335.0,    /* 1.0 dB Cut   */
    [0x2d] =  7770.0,    /* 0.5 dB Cut   */
    /* The Win95 drivers use D5-D0 = 1D instead of 2D, datasheet erratum? */
    [0x1d] =  7770.0,    /* 0.5 dB Cut   */
    [0x2f] =  8230.0,    /*        Flat  */
                 0.0
};

static __inline double
sinc(const double x)
{
    return sin(M_PI * x) / (M_PI * x);
}

static void
recalc_pas16_filter(const int playback_freq)
{
    /* Cutoff frequency = playback / 2 */
    int          n;
    const double fC = ((double) playback_freq) / (double) FREQ_96000;
    double       gain = 0.0;

    for (n = 0; n < SB16_NCoef; n++) {
        /* Blackman window */
        const double w = 0.42 - (0.5 * cos((2.0 * n * M_PI) / (double) (SB16_NCoef - 1))) +
                         (0.08 * cos((4.0 * n * M_PI) / (double) (SB16_NCoef - 1)));
        /* Sinc filter */
        const double h = sinc(2.0 * fC * ((double) n - ((double) (SB16_NCoef - 1) / 2.0)));

        /* Create windowed-sinc filter */
        low_fir_pas16_coef[n] = w * h;
    }

    low_fir_pas16_coef[(SB16_NCoef - 1) / 2] = 1.0;

    for (n = 0; n < SB16_NCoef; n++)
        gain += low_fir_pas16_coef[n];

    /* Normalise filter, to produce unity gain */
    for (n = 0; n < SB16_NCoef; n++)
        low_fir_pas16_coef[n] /= gain;
}

#ifdef ENABLE_PAS16_LOG
int pas16_do_log = ENABLE_PAS16_LOG;

static void
pas16_log(const char *fmt, ...)
{
    va_list ap;

    if (pas16_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pas16_log(fmt, ...)
#endif

static uint8_t
pas16_in(uint16_t port, void *priv);
static void
pas16_out(uint16_t port, uint8_t val, void *priv);

static void
pas16_update_irq(pas16_t *pas16)
{
    if (pas16->midi_uart_out && (pas16->midi_stat & 0x18)) {
        pas16->irq_stat |= PAS16_INT_MIDI;
        if ((pas16->irq != -1) && (pas16->irq_ena & PAS16_INT_MIDI))
            picint(1 << pas16->irq);
    }
    if (pas16->midi_uart_in && (pas16->midi_stat & 0x04)) {
        pas16->irq_stat |= PAS16_INT_MIDI;
        if ((pas16->irq != -1) && (pas16->irq_ena & PAS16_INT_MIDI))
            picint(1 << pas16->irq);
    }
}

static uint8_t
pas16_in(uint16_t port, void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;
    scsi_bus_t *scsi_bus = NULL;
    uint8_t  ret   = 0xff;

    port -= pas16->base;

    switch (port) {
        case 0x0000 ... 0x0003:
            ret = pas16->opl.read(port + 0x0388, pas16->opl.priv);
            break;

        case 0x0800:
            ret = pas16->type ? pas16->audio_mixer : 0xff;
            break;
        case 0x0801:
            ret = pas16->irq_stat & 0xdf;
            break;
        case 0x0802:
            ret = pas16->audiofilt;
            break;
        case 0x0803:
            ret = pas16->irq_ena | 0x20;
            pas16_log("IRQ Mask read=%02x.\n", ret);
            break;

        case 0x0c02:
            ret = pas16->pcm_ctrl;
            break;

        case 0x1401:
        case 0x1403:
            ret = pas16->midi_ctrl;
            break;
        case 0x1402:
        case 0x1802:
            ret = 0;
            if (pas16->midi_uart_in) {
                if ((pas16->midi_data == 0xaa) && (pas16->midi_ctrl & 0x04))
                    ret = pas16->midi_data;
                else {
                    ret = pas16->midi_queue[pas16->midi_r];
                    if (pas16->midi_r != pas16->midi_w) {
                        pas16->midi_r++;
                        pas16->midi_r &= 0xff;
                    }
                }
                pas16->midi_stat &= ~0x04;
                pas16_update_irq(pas16);
            }
            break;

        case 0x1800:
            ret = pas16->midi_stat;
            break;
        case 0x1801:
            ret = pas16->fifo_stat;
            break;

        case 0x1c00 ... 0x1c03:    /* NCR5380 ports 0 to 3. */
        case 0x3c00 ... 0x3c03:    /* NCR5380 ports 4 to 7. */
            if (pas16->has_scsi)
                ret = ncr5380_read((port & 0x0003) | ((port & 0x2000) >> 11), &pas16->scsi->ncr);
            break;

        case 0x2401:    /* Board revision */
            ret = 0x00;
            break;

        case 0x4000:
            ret = pas16->timeout_count;
            break;
        case 0x4001:
            ret = pas16->timeout_status;
            break;

        case 0x5c00:
            if (pas16->has_scsi)
                ret = t128_read(0x1e00, pas16->scsi);
            break;
        case 0x5c01:
            if (pas16->has_scsi) {
                scsi_bus = &pas16->scsi->ncr.scsibus;
                /* Bits 0-6 must absolutely be set for SCSI hard disk drivers to work. */
                ret = (((scsi_bus->tx_mode != PIO_TX_BUS) && (pas16->scsi->status & 0x04)) << 7) | 0x7f;
            }
            break;
        case 0x5c03:
            if (pas16->has_scsi)
                ret = pas16->scsi->ncr.irq_state << 7;
            break;

        case 0x7c01:
            ret = pas16->enhancedscsi & ~0x01;
            break;

        case 0x8000:
            ret = pas16->sys_conf_1;
            break;
        case 0x8001:
            ret = pas16->sys_conf_2;
            break;
        case 0x8002:
            ret = pas16->sys_conf_3;
            break;
        case 0x8003:
            ret = pas16->sys_conf_4;
            break;

        case 0xbc00:
            ret = pas16->waitstates;
            break;
        case 0xbc02:
            ret = pas16->prescale_div;
            break;

        case 0xec03:
            /*
               Operation mode 1:
               - 1,0 = CD-ROM (1,1 = SCSI, 1,0 = Sony, 0,0 = N/A);
               -   2 = FM (1 = stereo, 0 = mono);
               -   3 = Code (1 = 16-bit, 0 = 8-bit).
             */
            ret = pas16->type ? pas16->type : 0x07;
            break;

        case 0xf000:
            ret = pas16->io_conf_1;
            break;
        case 0xf001:
            ret = pas16->io_conf_2;
            pas16_log("pas16_in : set PAS DMA %i\n", pas16->dma);
            break;
        case 0xf002:
            ret = pas16->io_conf_3;
            pas16_log("pas16_in : set PAS IRQ %i\n", pas16->irq);
            break;
        case 0xf003:
            ret = pas16->io_conf_4;
            break;

        case 0xf400:
            ret = (pas16->compat & 0xf3);

            if (pas16->dsp.sb_irqm8 || pas16->dsp.sb_irqm16 || pas16->dsp.sb_irqm401)
                ret |= 0x04;

            if (pas16->mpu->mode == M_UART)
                ret |= 0x08;
            break;
        case 0xf401:
            ret = pas16->compat_base;
            break;

        case 0xf802:
            ret = pas16->sb_irqdma;
            break;

        case 0xfc00:    /* Board model */
            /* PAS16 or PASPlus */
            ret = pas16->type ? 0x0c : 0x01;
            break;
        case 0xfc03:    /* Master mode read */
            /* AT bus, XT/AT timing */
            ret = 0x11;
            if (pas16->type)
                ret |= 0x20;
            break;

        default:
            break;
    }

    pas16_log("[%04X:%08X] PAS16: [R] %04X (%04X) = %02X\n",
              CS, cpu_state.pc, port + pas16->base, port, ret);

    return ret;
}

static void
pas16_change_pit_clock_speed(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;
    pitf_t *pit = (pitf_t *) pas16->pit;

    if (pas16->type && (pas16->sys_conf_1 & 0x02) && pas16->prescale_div) {
        pit_change_pas16_consts((double) pas16->prescale_div);
        if (pas16->sys_conf_3 & 0x02)
            pitf_set_pit_const(pit, PAS16CONST2);
        else
            pitf_set_pit_const(pit, PAS16CONST);
    } else
        pitf_set_pit_const(pit, PITCONST);
}

static void
pas16_io_handler(pas16_t *pas16, int set)
{
    if (pas16->base != 0x0000) {
        for (uint32_t addr = 0x0000; addr <= 0xffff; addr += 0x0400) {
            pas16_log("%04X-%04X: %i\n", pas16->base + addr, pas16->base + addr + 3, set);
            if (addr != 0x1000)
                io_handler(set, pas16->base + addr, 0x0004,
                           pas16_in, NULL, NULL, pas16_out, NULL, NULL, pas16);
        }

        pitf_handler(set, pas16->base + 0x1000, 0x0004, pas16->pit);
    }
}

static void
pas16_reset_pcm(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    pas16->pcm_ctrl = 0x00;

    pas16->stereo_lr = 0;

    pas16->irq_stat &= 0xd7;

    if ((pas16->irq != -1) && !pas16->irq_stat)
        picintc(1 << pas16->irq);
}

static void
lmc1982_recalc(nsc_mixer_t *mixer)
{
    /* LMC1982CIN */
    /* According to the Windows 95 driver, the two volumes are swapped. */
    if ((mixer->lmc1982_regs[LMC1982_REG_ISELECT] & LMC1982_ISELECT_MASK) == LMC1982_ISELECT_I2) {
        switch (mixer->lmc1982_regs[LMC1982_REG_MODE] & LMC1982_MODE_MASK) {
            case LMC1982_MODE_MONO_L:
                mixer->master_l = mixer->master_r = lmc1982_att_2dbstep_6bits[mixer->lmc1982_regs[LMC1982_REG_VOL_R]] / 32767.0;
                break;
            case LMC1982_MODE_STEREO:
                mixer->master_l  = lmc1982_att_2dbstep_6bits[mixer->lmc1982_regs[LMC1982_REG_VOL_R]] / 32767.0;
                mixer->master_r  = lmc1982_att_2dbstep_6bits[mixer->lmc1982_regs[LMC1982_REG_VOL_L]] / 32767.0;
                break;
            case LMC1982_MODE_MONO_R:
            case LMC1982_MODE_MONO_R_2:
                mixer->master_l = mixer->master_r = lmc1982_att_2dbstep_6bits[mixer->lmc1982_regs[LMC1982_REG_VOL_L]] / 32767.0;
                break;
            default:
                mixer->master_l = mixer->master_r = 0.0;
                break;
        }

        mixer->bass      = mixer->lmc1982_regs[LMC1982_REG_BASS] & 0x0f;
        mixer->treble    = mixer->lmc1982_regs[LMC1982_REG_TREBLE] & 0x0f;
    } else {
        mixer->master_l  = mixer->master_r  = 0.0;

        mixer->bass      = 0x06;
        mixer->treble    = 0x06;
    }
}

static void
lmc835_recalc(nsc_mixer_t *mixer)
{
    /* LMC835N */
    /* Channel A (1-7) */
    const double *lmc835_att = (const double *) ((mixer->lmc835_regs[LMC835_REG_MODE] & 0x20) ?
                                                lmc835_att_05dbstep_7bits : lmc835_att_1dbstep_7bits);

    mixer->fm_l      = lmc835_att[mixer->lmc835_regs[LMC835_REG_FM_L] & 0x7f] / 32767.0;
    mixer->imixer_l  = lmc835_att[mixer->lmc835_regs[LMC835_REG_IMIXER_L] & 0x7f] / 32767.0;
    mixer->line_l    = lmc835_att[mixer->lmc835_regs[LMC835_REG_LINE_L] & 0x7f] / 32767.0;
    mixer->cd_l      = lmc835_att[mixer->lmc835_regs[LMC835_REG_CDROM_L] & 0x7f] / 32767.0;
    mixer->mic_l     = lmc835_att[mixer->lmc835_regs[LMC835_REG_MIC_L] & 0x7f] / 32767.0;
    mixer->pcm_l     = lmc835_att[mixer->lmc835_regs[LMC835_REG_PCM_L] & 0x7f] / 32767.0;
    mixer->speaker_l = lmc835_att[mixer->lmc835_regs[LMC835_REG_SPEAKER_L] & 0x7f] / 32767.0;

    /* Channel B (8-e) */
    lmc835_att       = (const double *) ((mixer->lmc835_regs[LMC835_REG_MODE] & 0x08) ?
                                        lmc835_att_05dbstep_7bits : lmc835_att_1dbstep_7bits);

    mixer->fm_r      = lmc835_att[mixer->lmc835_regs[LMC835_REG_FM_R] & 0x7f] / 32767.0;
    mixer->imixer_r  = lmc835_att[mixer->lmc835_regs[LMC835_REG_IMIXER_R] & 0x7f] / 32767.0;
    mixer->line_r    = lmc835_att[mixer->lmc835_regs[LMC835_REG_LINE_R] & 0x7f] / 32767.0;
    mixer->cd_r      = lmc835_att[mixer->lmc835_regs[LMC835_REG_CDROM_R] & 0x7f] / 32767.0;
    mixer->mic_r     = lmc835_att[mixer->lmc835_regs[LMC835_REG_MIC_R] & 0x7f] / 32767.0;
    mixer->pcm_r     = lmc835_att[mixer->lmc835_regs[LMC835_REG_PCM_R] & 0x7f] / 32767.0;
    mixer->speaker_r = lmc835_att[mixer->lmc835_regs[LMC835_REG_SPEAKER_R] & 0x7f] / 32767.0;
}

static void
mv508_mixer_recalc(mv508_mixer_t *mixer)
{
    mixer->fm_l      = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_FM_L]] / 32767.0;
    mixer->fm_r      = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_FM_R]] / 32767.0;
    mixer->imixer_l  = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_IMIXER_L]] / 32767.0;
    mixer->imixer_r  = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_IMIXER_R]] / 32767.0;
    mixer->line_l    = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_LINE_L]] / 32767.0;
    mixer->line_r    = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_LINE_R]] / 32767.0;
    mixer->cd_l      = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_CDROM_L]] / 32767.0;
    mixer->cd_r      = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_CDROM_R]] / 32767.0;
    mixer->mic_l     = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_MIC_L]] / 32767.0;
    mixer->mic_r     = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_MIC_R]] / 32767.0;
    mixer->pcm_l     = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_PCM_L]] / 32767.0;
    mixer->pcm_r     = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_PCM_R]] / 32767.0;
    mixer->speaker_l = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_SPEAKER_L]] / 32767.0;
    mixer->speaker_r = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_SPEAKER_R]] / 32767.0;
    mixer->sb_l      = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_SB_L]] / 32767.0;
    mixer->sb_r      = mva508_att_2dbstep_5bits[mixer->regs[0][MV508_REG_SB_R]] / 32767.0;

    mixer->master_l  = mva508_att_1dbstep_6bits[mixer->regs[2][MV508_REG_MASTER_A_L]] / 32767.0;
    mixer->master_r  = mva508_att_1dbstep_6bits[mixer->regs[2][MV508_REG_MASTER_A_R]] / 32767.0;

    mixer->bass      = mixer->regs[2][MV508_REG_BASS] & 0x0f;
    mixer->treble    = mixer->regs[2][MV508_REG_TREBLE] & 0x0f;
}

static void
pas16_nsc_mixer_reset(nsc_mixer_t *mixer)
{
    mixer->lmc1982_regs[LMC1982_REG_ISELECT] = 0x01;
    mixer->lmc1982_regs[LMC1982_REG_LES]     = 0x00;
    mixer->lmc1982_regs[LMC1982_REG_BASS]    = mixer->lmc1982_regs[LMC1982_REG_TREBLE]  = 0x06;
    mixer->lmc1982_regs[LMC1982_REG_VOL_L]   = mixer->lmc1982_regs[LMC1982_REG_VOL_R]   = 0x00; /*0x28*/ /*Note by TC1995: otherwise the volume gets lowered too much*/
    mixer->lmc1982_regs[LMC1982_REG_MODE]    = 0x05;

    lmc1982_recalc(mixer);

    mixer->lmc835_regs[LMC835_REG_MODE]      = 0x00;

    mixer->lmc835_regs[LMC835_REG_FM_L]      = mixer->lmc835_regs[LMC835_REG_FM_R]      = 0x69;
    mixer->lmc835_regs[LMC835_REG_IMIXER_L]  = mixer->lmc835_regs[LMC835_REG_IMIXER_R]  = 0x29;
    mixer->lmc835_regs[LMC835_REG_LINE_L]    = mixer->lmc835_regs[LMC835_REG_LINE_R]    = 0x69;
    mixer->lmc835_regs[LMC835_REG_CDROM_L]   = mixer->lmc835_regs[LMC835_REG_CDROM_R]   = 0x69;
    mixer->lmc835_regs[LMC835_REG_MIC_L]     = mixer->lmc835_regs[LMC835_REG_MIC_R]     = 0x44;
    mixer->lmc835_regs[LMC835_REG_PCM_L]     = mixer->lmc835_regs[LMC835_REG_PCM_R]     = 0x29;
    mixer->lmc835_regs[LMC835_REG_SPEAKER_L] = mixer->lmc835_regs[LMC835_REG_SPEAKER_R] = 0x06;

    lmc835_recalc(mixer);
}

static void
pas16_mv508_mixer_reset(mv508_mixer_t *mixer)
{
    /* Based on the Linux driver - TODO: The actual card's defaults. */
    mixer->regs[0][MV508_REG_FM_L]       = mixer->regs[0][MV508_REG_FM_R]       = 0x18;
    mixer->regs[0][MV508_REG_IMIXER_L]   = mixer->regs[0][MV508_REG_IMIXER_R]   = 0x1f;
    mixer->regs[0][MV508_REG_LINE_L]     = mixer->regs[0][MV508_REG_LINE_R]     = 0x17;
    mixer->regs[0][MV508_REG_CDROM_L]    = mixer->regs[0][MV508_REG_CDROM_R]    = 0x17;
    mixer->regs[0][MV508_REG_MIC_L]      = mixer->regs[0][MV508_REG_MIC_R]      = 0x17;
    mixer->regs[0][MV508_REG_PCM_L]      = mixer->regs[0][MV508_REG_PCM_R]      = 0x17;
    mixer->regs[0][MV508_REG_SPEAKER_L]  = mixer->regs[0][MV508_REG_SPEAKER_R]  = 0x0f;
    mixer->regs[0][MV508_REG_SB_L]       = mixer->regs[0][MV508_REG_SB_R]       = 0x17;

    mixer->regs[2][MV508_REG_MASTER_A_L] = mixer->regs[2][MV508_REG_MASTER_A_R] = 0x1f;

    mixer->regs[2][MV508_REG_BASS]       = 0x06;
    mixer->regs[2][MV508_REG_TREBLE]     = 0x06;

    mv508_mixer_recalc(mixer);
}

static void
pas16_reset_regs(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;
    nsc_mixer_t *nsc_mixer = &pas16->nsc_mixer;
    mv508_mixer_t *mv508_mixer = &pas16->mv508_mixer;
    pitf_t *pit = (pitf_t *) pas16->pit;

    if (pas16->irq != -1)
        picintc(1 << pas16->irq);

    pas16->sys_conf_1 &= 0xfd;

    pas16->sys_conf_2 = 0x00;
    pas16->sys_conf_3 = 0x00;

    pas16->prescale_div = 0x00;

    pitf_set_pit_const(pit, PITCONST);

    pas16->audiofilt = 0x00;
    pas16->filter = 0;

    pitf_ctr_set_gate(pit, 0, 0);
    pitf_ctr_set_gate(pit, 1, 0);

    pas16_reset_pcm(pas16);
    pas16->dma8_ff = 0;

    pas16->irq_ena = 0x00;
    pas16->irq_stat = 0x00;

    if (pas16->type)
        pas16_mv508_mixer_reset(mv508_mixer);
    else
        pas16_nsc_mixer_reset(nsc_mixer);
}

static void
pas16_reset_common(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    pas16_reset_regs(pas16);

    if (pas16->irq != -1)
        picintc(1 << pas16->irq);

    pas16_io_handler(pas16, 0);
    pas16->base = 0x0000;
}

static void
pas16_reset(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    pas16_reset_common(priv);

    pas16->board_id = 0;
    pas16->master_ff = 0;

    pas16->base = 0x0388;
    pas16_io_handler(pas16, 1);

    pas16->new_base = 0x0388;

    pas16->sb_compat_base = 0x0220;
    pas16->compat = 0x02;
    pas16->compat_base = 0x02;
    sb_dsp_setaddr(&pas16->dsp, pas16->sb_compat_base);
}

static int
pas16_irq_convert(uint8_t val)
{
    int ret = val;

    if (ret == 0)
        ret = -1;
    else if (ret <= 6)
        ret++;
    else if (ret < 0x0b)
        ret += 3;
    else
        ret += 4;

    return ret;
}

static void
lmc1982_update_reg(nsc_mixer_t *mixer)
{
    pas16_log("LMC1982CIN register %02X = %04X\n",
              mixer->im_data[LMC1982_ADDR], mixer->im_data[LMC1982_DATA]);

    if ((mixer->im_data[LMC1982_ADDR] & LMC1982_REG_MASK) == LMC1982_REG_VALID) {
        mixer->im_data[LMC1982_ADDR] &= ~LMC1982_REG_MASK;
        mixer->lmc1982_regs[mixer->im_data[LMC1982_ADDR]] = mixer->im_data[LMC1982_DATA] & 0xff;
        lmc1982_recalc(mixer);
    }

    mixer->im_state = STATE_SM_IDLE;
}

static void
lmc835_update_reg(nsc_mixer_t *mixer)
{
    pas16_log("LMC835N register %02X = %02X\n",
              mixer->im_data[LMC835_ADDR], mixer->im_data[LMC835_DATA]);

    mixer->lmc835_regs[LMC835_REG_MODE] = mixer->im_data[LMC835_ADDR] & 0xf0;
    mixer->im_data[LMC835_ADDR] &= 0x0f;
    if ((mixer->im_data[LMC835_ADDR] >= 0x01) && (mixer->im_data[LMC835_ADDR] <= 0x0e))
        mixer->lmc835_regs[mixer->im_data[LMC835_ADDR] & 0x0f] = mixer->im_data[LMC835_DATA];
    lmc835_recalc(mixer);
}

static void
pas16_scsi_callback(void *priv)
{
    pas16_t *      pas16 = (pas16_t *) priv;
    t128_t  *      dev   = pas16->scsi;
    scsi_bus_t *   scsi_bus = &dev->ncr.scsibus;

    t128_callback(pas16->scsi);

    if ((scsi_bus->tx_mode != PIO_TX_BUS) && (dev->status & 0x04)) {
        timer_stop(&pas16->scsi_timer);
        pas16->timeout_status &= 0x7f;
    }
}

static void
pas16_timeout_callback(void *priv)
{
    pas16_t *      pas16       = (pas16_t *) priv;

    pas16->timeout_status |= 0x80;

    if ((pas16->timeout_status & 0x08) && (pas16->irq != -1))
        picint(1 << pas16->irq);

    timer_advance_u64(&pas16->scsi_timer, (pas16->timeout_count & 0x3f) * PASSCSICONST);
}

static void
pas16_out(uint16_t port, uint8_t val, void *priv)
{
    pas16_t *      pas16       = (pas16_t *) priv;
    nsc_mixer_t *  nsc_mixer   = &pas16->nsc_mixer;
    mv508_mixer_t *mv508_mixer = &pas16->mv508_mixer;

    pas16_log("[%04X:%08X] PAS16: [W] %04X (%04X) = %02X\n",
              CS, cpu_state.pc, port, port - pas16->base, val);

    port -= pas16->base;

    switch (port) {
        case 0x0000 ... 0x0003:
            pas16->opl.write(port + 0x0388, val, pas16->opl.priv);
            break;

        case 0x0400 ... 0x0402:
            break;
        case 0x0403:
            if (val & MV508_ADDRESS)
		mv508_mixer->index = val & ~MV508_ADDRESS;
            else {
                uint8_t bank;
                uint8_t mask;

                pas16_log("MVA508 register %02X = %02X\n",
                          mv508_mixer->index, val);

                if (mv508_mixer->index & MV508_MIXER) {
		    bank = !!(val & MV508_INPUT_MIX);
                    mask = 0x1f;
                } else {
		    bank = 2;
                    mask = 0x3f;
                }

                if (mv508_mixer->index & MV508_CHANNEL)
                    mv508_mixer->regs[bank][mv508_mixer->index] = val & mask;
                else {
                    mv508_mixer->regs[bank][mv508_mixer->index | MV508_LEFT] = val & mask;
                    mv508_mixer->regs[bank][mv508_mixer->index | MV508_RIGHT] = val & mask;
                }

                mv508_mixer_recalc(mv508_mixer);
            }
            break;

        case 0x0800:
            if (pas16->type && !(val & SERIAL_MIXER_RESET_PCM)) {
                pas16->audio_mixer = val;
                pas16_reset_pcm(pas16);
            } else if (!pas16->type) {
                switch (nsc_mixer->im_state) {
                    default:
                        break;
                    case STATE_SM_IDLE:
                        /* Transmission initiated. */
                        if (val & SERIAL_MIXER_IDENT) {
                            if (!(val & SERIAL_MIXER_CLOCK) && (pas16->audio_mixer & SERIAL_MIXER_CLOCK)) {
                                /* Prepare for receiving LMC835N data. */
                                nsc_mixer->im_data[LMC835_DATA] = 0x0000;
                                nsc_mixer->im_state |= STATE_LMC835_DATA;
                            }
                        } else {
                            if ((pas16->audio_mixer & SERIAL_MIXER_IDENT)) {
                                /*
                                   Prepare for receiving the LMC1982CIN address.
                                 */
                                nsc_mixer->im_data[LMC1982_ADDR] = 0x0000;
                                nsc_mixer->im_state |= STATE_LMC1982_ADDR;
                            }
                            if ((val & SERIAL_MIXER_CLOCK) && !(pas16->audio_mixer & SERIAL_MIXER_CLOCK)) {
                                /*
                                   Clock the least siginificant bit of the LMC1982CIN address.
                                 */
                                nsc_mixer->im_data[LMC1982_ADDR] |=
                                    ((val & SERIAL_MIXER_DATA) << (nsc_mixer->im_state & STATE_DATA_MASK));
                                nsc_mixer->im_state++;
                            }
                        }
                        break;
                    case STATE_LMC1982_ADDR_0:
                        if (val & SERIAL_MIXER_IDENT) {
                            /*
                               IDENT went high in LM1982CIN address state 0,
                               behave as if we were in the idle state.
                             */
                            if (!(val & SERIAL_MIXER_CLOCK) && (pas16->audio_mixer & SERIAL_MIXER_CLOCK)) {
                                nsc_mixer->im_data[LMC835_DATA] = 0x0000;
                                nsc_mixer->im_state = STATE_LMC835_DATA_0;
                            }
                        } else if ((val & SERIAL_MIXER_CLOCK) &&
                                   !(pas16->audio_mixer & SERIAL_MIXER_CLOCK)) {
                            /*
                               Clock the least siginificant bit of the LMC1982CIN address.
                             */
                            nsc_mixer->im_data[LMC1982_ADDR] |=
                                ((val & SERIAL_MIXER_DATA) << (nsc_mixer->im_state & STATE_DATA_MASK));
                            nsc_mixer->im_state++;
                        }
                        break;
                    case STATE_LMC1982_ADDR_1 ... STATE_LMC1982_ADDR_7:
                        if ((val & 0x02) && !(pas16->audio_mixer & 0x02)) {
                            /*
                               Clock the next bit of the LMC1982CIN address.
                             */
                            nsc_mixer->im_data[LMC1982_ADDR] |=
                                ((val & SERIAL_MIXER_DATA) << (nsc_mixer->im_state & STATE_DATA_MASK));
                            nsc_mixer->im_state++;
                        }
                        break;
                    case STATE_LMC1982_ADDR_OVER:
                        /*
                           Prepare for receiving the LMC1982CIN data.
                         */
                        nsc_mixer->im_data[LMC1982_DATA] = 0x0000;
                        nsc_mixer->im_state = STATE_LMC1982_DATA_0;
                        break;
                    case STATE_LMC1982_DATA_0 ... STATE_LMC1982_DATA_7:
                    case STATE_LMC1982_DATA_9 ... STATE_LMC1982_DATA_F:
                        if ((val & SERIAL_MIXER_CLOCK) && !(pas16->audio_mixer & SERIAL_MIXER_CLOCK)) {
                            /*
                               Clock the next bit of the LMC1982CIN data.
                             */
                            nsc_mixer->im_data[LMC1982_DATA] |=
                                ((val & SERIAL_MIXER_DATA) << (nsc_mixer->im_state & STATE_DATA_MASK_W));
                            nsc_mixer->im_state++;
                        }
                        break;
                    case STATE_LMC1982_DATA_8:
                        if (val & SERIAL_MIXER_IDENT) {
                            if (!(pas16->audio_mixer & SERIAL_MIXER_IDENT))
                                /*
                                   LMC1982CIN data transfer ended after 8 bits, process the data and
                                   reset the state back to idle.
                                 */
                                lmc1982_update_reg(nsc_mixer);
                            else if ((val & SERIAL_MIXER_CLOCK) &&
                                     !(pas16->audio_mixer & SERIAL_MIXER_CLOCK)) {
                                /*
                                   Clock the next bit of the LMC1982CIN data.
                                 */
                                nsc_mixer->im_data[LMC1982_DATA] |=
                                    ((val & SERIAL_MIXER_DATA) << (nsc_mixer->im_state & STATE_DATA_MASK_W));
                                nsc_mixer->im_state++;
                            }
                        }
                        break;
                    case STATE_LMC1982_DATA_OVER:
                        if ((val & SERIAL_MIXER_IDENT) && !(pas16->audio_mixer & SERIAL_MIXER_IDENT))
                            /*
                               LMC1982CIN data transfer ended, process the data and reset the state
                               back to idle.
                             */
                            lmc1982_update_reg(nsc_mixer);
                        break;
                    case STATE_LMC835_DATA_0 ... STATE_LMC835_DATA_7:
                        if ((val & SERIAL_MIXER_CLOCK) && !(pas16->audio_mixer & SERIAL_MIXER_CLOCK)) {
                            /*
                               Clock the next bit of the LMC835N data.
                             */
                            nsc_mixer->im_data[LMC835_DATA] |=
                                ((val &  SERIAL_MIXER_DATA) << (nsc_mixer->im_state & STATE_DATA_MASK));
                            nsc_mixer->im_state++;
                        }
                        break;
                    case STATE_LMC835_DATA_OVER:
                        if ((val & SERIAL_MIXER_STROBE) && !(pas16->audio_mixer & SERIAL_MIXER_STROBE)) {
                            if (nsc_mixer->im_data[LMC835_DATA] & LMC835_FLAG_ADDR)
                                /*
                                   The LMC835N data is an address, copy it into its own space for usage
                                   when processing it at the end and strip bits 7 (it's the address/data
                                   indicator) and 6 (it's a "don't care" bit).
                                 */
                                nsc_mixer->im_data[LMC835_ADDR] = nsc_mixer->im_data[LMC835_DATA] & 0x7f;
                            else
                                lmc835_update_reg(nsc_mixer);
                            nsc_mixer->im_state = STATE_SM_IDLE;
                        }
                        break;
                }

                pas16->audio_mixer = val;
            }
            break;
        case 0x0801:
            pas16->irq_stat &= ~val;
            if ((pas16->irq != -1) && !(pas16->irq_stat & 0x1f))
                picintc(1 << pas16->irq);
            break;
        case 0x0802:
            pas16_update(pas16);

            pitf_ctr_set_gate(pas16->pit, 1, !!(val & 0x80));
            pitf_ctr_set_gate(pas16->pit, 0, !!(val & 0x40));

            pas16->stereo_lr = 0;
            pas16->dma8_ff = 0;

            if ((val & 0x20) && !(pas16->audiofilt & 0x20)) {
                pas16_log("Reset.\n");
                pas16_reset_regs(pas16);
            }

            pas16->audiofilt = val;

            if (val & 0x1f) {
                pas16->filter = 1;
                switch (val & 0x1f) {
                    default:
                        pas16->filter = 0;
                        break;
                    case 0x01:
                        recalc_pas16_filter(17897);
                        break;
                    case 0x02:
                        recalc_pas16_filter(15909);
                        break;
                    case 0x04:
                        recalc_pas16_filter(2982);
                        break;
                    case 0x09:
                        recalc_pas16_filter(11931);
                        break;
                    case 0x11:
                        recalc_pas16_filter(8948);
                        break;
                    case 0x19:
                        recalc_pas16_filter(5965);
                        break;
                }
            } else
                pas16->filter = 0;
            break;
        case 0x0803:
            pas16->irq_ena = val & 0x1f;
            pas16->irq_stat &= ((val & 0x1f) | 0xe0);

            if ((pas16->irq != -1) && !(pas16->irq_stat & 0x1f))
                picintc(1 << pas16->irq);
            break;

        case 0x0c00:
        case 0x0c01:
            pas16_update(pas16);
            break;
        case 0x0c02:
            if ((val & PAS16_PCM_ENA) && !(pas16->pcm_ctrl & PAS16_PCM_ENA)) {
                /* Guess */
                pas16->stereo_lr = 0;
                pas16->irq_stat &= 0xd7;
                /* Needed for 8-bit DMA to work correctly on a 16-bit DMA channel. */
                pas16->dma8_ff = 0;
            }

            pas16->pcm_ctrl = val;
            pas16_log("Now in: %s (%02X)\n", (pas16->pcm_ctrl & PAS16_PCM_MONO) ? "Mono" : "Stereo", val);
            break;

        case 0x1401:
        case 0x1403:
            pas16->midi_ctrl = val;
            if ((val & 0x60) == 0x60) {
                pas16->midi_uart_out = 0;
                pas16->midi_uart_in = 0;
            } else if ((val & 0x1c) == 0x04)
                pas16->midi_uart_in = 1;
            else
                pas16->midi_uart_out = 1;

            pas16_update_irq(pas16);
            break;
        case 0x1402:
        case 0x1802:
            pas16->midi_data = val;
            pas16_log("UART OUT=%d.\n", pas16->midi_uart_out);
            if (pas16->midi_uart_out)
                midi_raw_out_byte(val);
            break;

        case 0x1800:
            pas16->midi_stat = val;
            pas16_update_irq(pas16);
            break;
        case 0x1801:
            pas16->fifo_stat = val;
            break;

        case 0x1c00 ... 0x1c03:    /* NCR5380 ports 0 to 3. */
        case 0x3c00 ... 0x3c03:    /* NCR5380 ports 4 to 7. */
            if (pas16->has_scsi)
                ncr5380_write((port & 0x0003) | ((port & 0x2000) >> 11), val, &pas16->scsi->ncr);
            break;

        case 0x4000:
            if (pas16->has_scsi) {
                pas16->timeout_count = val;
                if (timer_is_enabled(&pas16->scsi_timer))
                    timer_disable(&pas16->scsi_timer);
                if ((val & 0x3f) > 0x00)
                    timer_set_delay_u64(&pas16->scsi_timer, (val & 0x3f) * PASSCSICONST);
            }
            break;

        case 0x4001:
            if (pas16->has_scsi) {
                pas16->timeout_status = val & 0x7f;
                if (pas16->scsi_irq != -1)
                    picintc(1 << pas16->scsi_irq);
            }
            break;

        case 0x5c00:
            if (pas16->has_scsi)
                t128_write(0x1e00, val, pas16->scsi);
            break;
        case 0x5c03:
            if (pas16->has_scsi) {
                if (val & 0x80) {
                    pas16->scsi->ncr.irq_state = 0;
                    if (pas16->scsi_irq != -1)
                        picintc(1 << pas16->scsi_irq);
                }
            }
            break;

        case 0x7c01:
            pas16->enhancedscsi = val;
            break;

        case 0x8000:
            if ((val & 0xc0) && !(pas16->sys_conf_1 & 0xc0)) {
                pas16_log("Reset.\n");
                val = 0x00;
                pas16_reset_common(pas16);
                pas16->base = pas16->new_base;
                pas16_io_handler(pas16, 1);
            }

            pas16->sys_conf_1 = val;
            pas16_change_pit_clock_speed(pas16);
            pas16_log("Now in: %s mode\n", (pas16->sys_conf_1 & 0x02) ? "native" : "compatibility");
            break;
        case 0x8001:
            pas16->sys_conf_2 = val;
            pas16_log("Now in: %i bits (%02X)\n",
                      (pas16->sys_conf_2 & 0x04) ? ((pas16->sys_conf_2 & 0x08) ? 12 : 16) : 8, val);
            break;
        case 0x8002:
            pas16->sys_conf_3 = val;
            pas16_change_pit_clock_speed(pas16);
            pas16_log("Use 1.008 MHz clok for PCM: %c\n", (val & 0x02) ? 'Y' : 'N');
            break;
        case 0x8003:
            pas16->sys_conf_4 = val;
            if (pas16->has_scsi && (pas16->scsi_irq != -1) && !(val & 0x20))
                picintc(1 << pas16->scsi_irq);
            break;

        case 0xbc00:
            pas16->waitstates = val;
            break;
        case 0xbc02:
            pas16->prescale_div = val;
            pas16_change_pit_clock_speed(pas16);
            pas16_log("Prescale divider now: %i\n", val);
            break;

        case 0xf000:
            pas16->io_conf_1 = val;
            break;
        case 0xf001:
            pas16->io_conf_2 = val;
            pas16->dma       = pas16_dmas[val & 0x7];
            pas16_change_pit_clock_speed(pas16);
            pas16_log("pas16_out : set PAS DMA %i\n", pas16->dma);
            break;
        case 0xf002:
            pas16->io_conf_3 = val;
            if (pas16->irq != -1)
                picintc(1 << pas16->irq);
            pas16->irq       = pas16_irq_convert(val & 0x0f);
            if (pas16->has_scsi) {
                if (pas16->scsi_irq != -1)
                    picintc(1 << pas16->scsi_irq);
                pas16->scsi_irq  = pas16_irq_convert(val >> 4);
                ncr5380_set_irq(&pas16->scsi->ncr, pas16->scsi_irq);
            }

            pas16_log("pas16_out : set PAS IRQ %i, val=%02x\n", pas16->irq, val & 0x0f);
            break;
        case 0xf003:
            pas16->io_conf_4 = val;
            break;

        case 0xf400:
            pas16->compat = val & 0xf3;
            pas16_log("PCM compression is now %sabled\n", (val & 0x10) ? "en" : "dis");
            if (pas16->compat & 0x02)
                sb_dsp_setaddr(&pas16->dsp, pas16->sb_compat_base);
            else
                sb_dsp_setaddr(&pas16->dsp, 0);
            if (pas16->compat & 0x01)
                mpu401_change_addr(pas16->mpu, ((pas16->compat_base & 0xf0) | 0x300));
            else
                mpu401_change_addr(pas16->mpu, 0);
            break;
        case 0xf401:
            pas16->compat_base = val;
            pas16->sb_compat_base = ((pas16->compat_base & 0xf) << 4) | 0x200;
            pas16_log("SB Compatibility base: %04X\n", pas16->sb_compat_base);
            if (pas16->compat & 0x02)
                sb_dsp_setaddr(&pas16->dsp, pas16->sb_compat_base);
            if (pas16->compat & 0x01)
                mpu401_change_addr(pas16->mpu, ((pas16->compat_base & 0xf0) | 0x300));
            break;

        case 0xf802:
            pas16->sb_irqdma = val;
            mpu401_setirq(pas16->mpu, pas16_sb_irqs[val & 7]);
            sb_dsp_setirq(&pas16->dsp, pas16_sb_irqs[(val >> 3) & 7]);
            sb_dsp_setdma8(&pas16->dsp, pas16_sb_dmas[(val >> 6) & 3]);
            pas16_log("pas16_out : set SB IRQ %i DMA %i.\n", pas16_sb_irqs[(val >> 3) & 7],
                      pas16_sb_dmas[(val >> 6) & 3]);
            break;

        default:
            pas16_log("pas16_out : unknown %04X\n", port);
    }
}

/*
   8-bit mono:
    - 8-bit DMA : On every timer 0 over, read the 8-bit sample and ctr_clock();
      (One ctr_clock() per timer 0 over)
    - 16-bit DMA: On every even timer 0 over, read two 8-bit samples at once and ctr_clock();
                  On every odd timer 0 over, read the MSB of the previously read sample word.
      (One ctr_clock() per two timer 0 overs)
   8-bit stereo:
    - 8-bit DMA : On every timer 0, read two 8-bit samples and ctr_clock() twice;
      (Two ctr_clock()'s per timer 0 over)
    - 16-bit DMA: On every timer 0, read two 8-bit samples and ctr_clock() once.
      (One ctr_clock() per timer 0 over)
   16-bit mono (to be verified):
    - 8-bit DMA : On every timer 0, read one 16-bit sample and ctr_clock() twice;
      (Two ctr_clock()'s per timer 0 over)
    - 16-bit DMA: On every timer 0, read one 16-bit sample and ctr_clock() once.
      (One ctr_clock() per timer 0 over)
   16-bit stereo:
    - 8-bit DMA : On every timer 0, read one 16-bit sample and ctr_clock() twice;
      (Two ctr_clock()'s per timer 0 over)
    - 16-bit DMA: On every timer 0, read one 16-bit sample and ctr_clock() twice.
      (Two ctr_clock()'s per timer 0 over)

   What we can conclude from this is:
    - Maximum 16 bits per timer 0 over;
    - A 8-bit sample always takes one ctr_clock() tick, unless it has been read
      alongside the previous sample;
    - A 16-bit sample always takes two ctr_clock() ticks.
 */
static uint16_t
pas16_dma_channel_read(pas16_t *pas16, UNUSED(int channel))
{
    int status;
    uint16_t ret;

    if (pas16->pcm_ctrl & PAS16_PCM_DMA_ENA) {
        if (pas16->dma >= 5) {
            dma_channel_advance(pas16->dma);
            status = dma_channel_read_only(pas16->dma);
        } else
            status = dma_channel_read(pas16->dma);
        ret = (status == DMA_NODATA) ? 0x0000 : (status & 0xffff);
    } else
        ret = 0x0000;

    return ret;
}

static uint16_t
pas16_dma_readb(pas16_t *pas16)
{
    const uint16_t ret = pas16_dma_channel_read(pas16, pas16->dma);

    pas16->ticks++;

    return ret;
}

static uint16_t
pas16_dma_readw(pas16_t *pas16, const uint8_t timer1_ticks)
{
    uint16_t ret;

    if (pas16->dma >= 5)
        ret = pas16_dma_channel_read(pas16, pas16->dma);
    else {
        ret = pas16_dma_channel_read(pas16, pas16->dma);
        ret |= (pas16_dma_channel_read(pas16, pas16->dma) << 8);
    }

    pas16->ticks += timer1_ticks;

    return ret;
}

static uint16_t
pas16_readdmab(pas16_t *pas16)
{
    if (pas16->dma >= 5) {
        if (pas16->dma8_ff)
            pas16->dma8_dat >>= 8;
        else
            pas16->dma8_dat = pas16_dma_readb(pas16);

        pas16->dma8_ff = !pas16->dma8_ff;
    } else
        pas16->dma8_dat = pas16_dma_readb(pas16);

    return ((pas16->dma8_dat & 0xff) ^ 0x80) << 8;
}

static uint16_t
pas16_readdmaw_mono(pas16_t *pas16)
{
    const uint16_t ret = pas16_dma_readw(pas16, 1 + (pas16->dma < 5));

    return ret;
}

static uint16_t
pas16_readdmaw_stereo(pas16_t *pas16)
{
    uint16_t ret;
    uint16_t ticks = (pas16->sys_conf_1 & 0x02) ? (1 + (pas16->dma < 5)) : 2;

    ret = pas16_dma_readw(pas16, ticks);

    return ret;
}

static uint16_t
pas16_readdma_mono(pas16_t *pas16)
{
    uint16_t ret;

    if (pas16->sys_conf_2 & 0x04) {
        ret = pas16_readdmaw_mono(pas16);

        if (pas16->sys_conf_2 & 0x08)
            ret &= 0xfff0;
    } else
        ret = pas16_readdmab(pas16);

    if (pas16->sys_conf_2 & PAS16_SC2_MSBINV)
        ret ^= 0x8000;

    return ret;
}

static uint16_t
pas16_readdma_stereo(pas16_t *pas16)
{
    uint16_t ret;

    if (pas16->sys_conf_2 & 0x04) {
        ret = pas16_readdmaw_stereo(pas16);

        if (pas16->sys_conf_2 & 0x08)
            ret &= 0xfff0;
    } else
        ret = pas16_readdmab(pas16);

    if (pas16->sys_conf_2 & PAS16_SC2_MSBINV)
        ret ^= 0x8000;

    return ret;
}

static void
pas16_pit_timer0(const int new_out, UNUSED(int old_out), void *priv)
{
    const pitf_t *pit   = (const pitf_t *) priv;
    pas16_t *     pas16 = (pas16_t *) pit->dev_priv;

    if (!pas16->pit->counters[0].gate)
        return;

    if (!dma_channel_readable(pas16->dma))
        return;

    pas16_update_irq(pas16);

    if (((pas16->pcm_ctrl & PAS16_PCM_ENA) == PAS16_PCM_ENA) && (pit->counters[1].m & 2) && new_out) {
        uint16_t temp;

        pas16->ticks = 0;

        if (pas16->pcm_ctrl & PAS16_PCM_MONO) {
            temp = pas16_readdma_mono(pas16);

            pas16->pcm_dat_l = pas16->pcm_dat_r = temp;
        } else {
            temp = pas16_readdma_stereo(pas16);

            if (pas16->sys_conf_1 & 0x02) {
                pas16->pcm_dat_l = temp;

                temp = pas16_readdma_stereo(pas16);

                pas16->pcm_dat_r = temp;
            } else {
                if (pas16->stereo_lr)
                    pas16->pcm_dat_r = temp;
                else
                    pas16->pcm_dat_l = temp;

                pas16->stereo_lr = !pas16->stereo_lr;
                pas16->irq_stat = (pas16->irq_stat & 0xdf) | (pas16->stereo_lr << 5);
            }
        }

        if (pas16->ticks) {
            for (uint16_t i = 0; i < pas16->ticks; i++)
                pitf_ctr_clock(pas16->pit, 1);

            pas16->ticks = 0;
        }

        pas16->irq_stat |= PAS16_INT_SAMP;
        if (pas16->irq_ena & PAS16_INT_SAMP) {
            pas16_log("INT SAMP.\n");
            if (pas16->irq != -1)
                picint(1 << pas16->irq);
        }

        pas16_update(pas16);
    }
}

static void
pas16_pit_timer1(const int new_out, UNUSED(int old_out), void *priv)
{
    const pitf_t *pit   = (pitf_t * )priv;
    pas16_t *     pas16 = (pas16_t *) pit->dev_priv;

    if (!pas16->pit->counters[1].gate)
        return;

    /* At new_out = 0, it's in the counter reload phase. */
    if ((pas16->pcm_ctrl & PAS16_PCM_ENA) && (pit->counters[1].m & 2) && new_out) {
        if (pas16->irq_ena & PAS16_INT_PCM) {
            pas16->irq_stat |= PAS16_INT_PCM;
            pas16_log("pas16_pcm_poll : cause IRQ %i %02X\n", pas16->irq, 1 << pas16->irq);
            if (pas16->irq != -1)
                picint(1 << pas16->irq);
        }
    }
}

static void
pas16_out_base(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    pas16_log("[%04X:%08X] PAS16: [W] %04X = %02X\n", CS, cpu_state.pc, port, val);

    if (pas16->master_ff && (pas16->board_id == pas16->this_id))
        pas16->new_base = val << 2;
    else if (!pas16->master_ff)
        pas16->board_id = val;

    pas16->master_ff = !pas16->master_ff;
}

static void
pas16_input_msg(void *priv, uint8_t *msg, uint32_t len)
{
    pas16_t  *pas16 = (pas16_t *) priv;

    if (pas16->sysex)
        return;

    if (pas16->midi_uart_in) {
        pas16->midi_stat |= 0x04;

        for (uint32_t i = 0; i < len; i++) {
            pas16->midi_queue[pas16->midi_w++] = msg[i];
            pas16->midi_w &= 0xff;
        }

        pas16_update_irq(pas16);
    }
}

static int
pas16_input_sysex(void *priv, uint8_t *buffer, uint32_t len, int abort)
{
    pas16_t  *pas16 = (pas16_t *) priv;

    if (abort) {
        pas16->sysex = 0;
        return 0;
    }
    pas16->sysex = 1;
    for (uint32_t i = 0; i < len; i++) {
        if (pas16->midi_r == pas16->midi_w)
            return (int) (len - i);
        pas16->midi_queue[pas16->midi_w++] = buffer[i];
        pas16->midi_w &= 0xff;
    }
    pas16->sysex = 0;
    return 0;
}

static void
pas16_update(pas16_t *pas16)
{
    if (!(pas16->audiofilt & PAS16_FILT_MUTE)) {
        for (; pas16->pos < sound_pos_global; pas16->pos++) {
            pas16->pcm_buffer[0][pas16->pos] = 0;
            pas16->pcm_buffer[1][pas16->pos] = 0;
        }
    } else {
        for (; pas16->pos < sound_pos_global; pas16->pos++) {
            pas16->pcm_buffer[0][pas16->pos] = (int16_t) pas16->pcm_dat_l;
            pas16->pcm_buffer[1][pas16->pos] = (int16_t) pas16->pcm_dat_r;
        }
    }
}

void
pasplus_get_buffer(int32_t *buffer, int len, void *priv)
{
    pas16_t *          pas16   = (pas16_t *) priv;
    const nsc_mixer_t *mixer   = &pas16->nsc_mixer;
    double             bass_treble;

    sb_dsp_update(&pas16->dsp);
    pas16_update(pas16);
    for (int c = 0; c < len * 2; c += 2) {
        double out_l = pas16->dsp.buffer[c];
        double out_r = pas16->dsp.buffer[c + 1];

        if (pas16->filter) {
            /* We divide by 3 to get the volume down to normal. */
            out_l += low_fir_pas16(0, (double) pas16->pcm_buffer[0][c >> 1]) * mixer->pcm_l;
            out_r += low_fir_pas16(1, (double) pas16->pcm_buffer[1][c >> 1]) * mixer->pcm_r;
        } else {
            out_l += ((double) pas16->pcm_buffer[0][c >> 1]) * mixer->pcm_l;
            out_r += ((double) pas16->pcm_buffer[1][c >> 1]) * mixer->pcm_r;
        }

        out_l *= mixer->master_l;
        out_r *= mixer->master_r;

        /* This is not exactly how one does bass/treble controls, but the end result is like it.
           A better implementation would reduce the CPU usage. */
        if (mixer->bass != 6) {
            bass_treble = lmc1982_bass_treble_4bits[mixer->bass];

            if (mixer->bass > 6) {
                out_l += (low_iir(0, 0, out_l) * bass_treble);
                out_r += (low_iir(0, 1, out_r) * bass_treble);
            } else if (mixer->bass < 6) {
                out_l = (out_l *bass_treble + low_cut_iir(0, 0, out_l) * (1.0 - bass_treble));
                out_r = (out_r *bass_treble + low_cut_iir(0, 1, out_r) * (1.0 - bass_treble));
            }
        }

        if (mixer->treble != 6) {
            bass_treble = lmc1982_bass_treble_4bits[mixer->treble];

            if (mixer->treble > 6) {
                out_l += (high_iir(0, 0, out_l) * bass_treble);
                out_r += (high_iir(0, 1, out_r) * bass_treble);
            } else if (mixer->treble < 6) {
                out_l = (out_l *bass_treble + high_cut_iir(0, 0, out_l) * (1.0 - bass_treble));
                out_r = (out_r *bass_treble + high_cut_iir(0, 1, out_r) * (1.0 - bass_treble));
            }
        }

        buffer[c] += (int32_t) out_l;
        buffer[c + 1] += (int32_t) out_r;
    }

    pas16->pos = 0;
    pas16->dsp.pos = 0;
}

void
pasplus_get_music_buffer(int32_t *buffer, int len, void *priv)
{
    const pas16_t *    pas16   = (const pas16_t *) priv;
    const nsc_mixer_t *mixer   = &pas16->nsc_mixer;
    const int32_t *    opl_buf = pas16->opl.update(pas16->opl.priv);
    double             bass_treble;

    for (int c = 0; c < len * 2; c += 2) {
        double out_l = (((double) opl_buf[c]) * mixer->fm_l) * 0.7171630859375;
        double out_r = (((double) opl_buf[c + 1]) * mixer->fm_r) * 0.7171630859375;

        /* TODO: recording CD, Mic with AGC or line in. Note: mic volume does not affect recording. */
        out_l *= mixer->master_l;
        out_r *= mixer->master_r;

        /* This is not exactly how one does bass/treble controls, but the end result is like it.
           A better implementation would reduce the CPU usage. */
        if (mixer->bass != 6) {
            bass_treble = lmc1982_bass_treble_4bits[mixer->bass];

            if (mixer->bass > 6) {
                out_l += (low_iir(1, 0, out_l) * bass_treble);
                out_r += (low_iir(1, 1, out_r) * bass_treble);
            } else if (mixer->bass < 6) {
                out_l = (out_l *bass_treble + low_cut_iir(1, 0, out_l) * (1.0 - bass_treble));
                out_r = (out_r *bass_treble + low_cut_iir(1, 1, out_r) * (1.0 - bass_treble));
            }
        }

        if (mixer->treble != 6) {
            bass_treble = lmc1982_bass_treble_4bits[mixer->treble];

            if (mixer->treble > 6) {
                out_l += (high_iir(1, 0, out_l) * bass_treble);
                out_r += (high_iir(1, 1, out_r) * bass_treble);
            } else if (mixer->treble < 6) {
                out_l = (out_l *bass_treble + high_cut_iir(1, 0, out_l) * (1.0 - bass_treble));
                out_r = (out_r *bass_treble + high_cut_iir(1, 1, out_r) * (1.0 - bass_treble));
            }
        }

        buffer[c] += (int32_t) out_l;
        buffer[c + 1] += (int32_t) out_r;
    }

    pas16->opl.reset_buffer(pas16->opl.priv);
}

void
pasplus_filter_cd_audio(int channel, double *buffer, void *priv)
{
    const pas16_t *    pas16  = (const pas16_t *) priv;
    const nsc_mixer_t *mixer  = &pas16->nsc_mixer;
    const double       cd     = channel ? mixer->cd_r : mixer->cd_l;
    const double       master = channel ? mixer->master_r : mixer->master_l;
    const int32_t      bass   = mixer->bass;
    const int32_t      treble = mixer->treble;
    double             c      = (*buffer) * cd * master;
    double             bass_treble;

    /* This is not exactly how one does bass/treble controls, but the end result is like it.
       A better implementation would reduce the CPU usage. */
    if (bass != 6) {
        bass_treble = lmc1982_bass_treble_4bits[bass];

        if (bass > 6)
            c += (low_iir(2, channel, c) * bass_treble);
        else
            c = (c * bass_treble + low_cut_iir(2, channel, c) * (1.0 - bass_treble));
    }

    if (treble != 6) {
        bass_treble = lmc1982_bass_treble_4bits[treble];

        if (treble > 6)
            c += (high_iir(2, channel, c) * bass_treble);
        else
            c = (c * bass_treble + high_cut_iir(2, channel, c) * (1.0 - bass_treble));
    }

    *buffer = c;
}

void
pasplus_filter_pc_speaker(int channel, double *buffer, void *priv)
{
    const pas16_t *    pas16  =  (pas16_t *) priv;
    const nsc_mixer_t *mixer  = &pas16->nsc_mixer;
    const double       spk    = channel ? mixer->speaker_r : mixer->speaker_l;
    const double       master = channel ? mixer->master_r : mixer->master_l;
    const int32_t      bass   = mixer->bass;
    const int32_t      treble = mixer->treble;
    double             c      = (*buffer) * spk * master;
    double             bass_treble;

    /* This is not exactly how one does bass/treble controls, but the end result is like it.
       A better implementation would reduce the CPU usage. */
    if (bass != 6) {
        bass_treble = lmc1982_bass_treble_4bits[bass];

        if (bass > 6)
            c += (low_iir(3, channel, c) * bass_treble);
        else
            c = (c * bass_treble + low_cut_iir(3, channel, c) * (1.0 - bass_treble));
    }

    if (treble != 6) {
        bass_treble = lmc1982_bass_treble_4bits[treble];

        if (treble > 6)
            c += (high_iir(3, channel, c) * bass_treble);
        else
            c = (c * bass_treble + high_cut_iir(3, channel, c) * (1.0 - bass_treble));
    }

    *buffer = c;
}

void
pas16_get_buffer(int32_t *buffer, int len, void *priv)
{
    pas16_t *            pas16 =  (pas16_t *) priv;
    const mv508_mixer_t *mixer   = &pas16->mv508_mixer;
    double               bass_treble;

    sb_dsp_update(&pas16->dsp);
    pas16_update(pas16);
    for (int c = 0; c < len * 2; c += 2) {
        double out_l = (pas16->dsp.buffer[c] * mixer->sb_l) / 3.0;
        double out_r = (pas16->dsp.buffer[c + 1] * mixer->sb_r) / 3.0;

        if (pas16->filter) {
            /* We divide by 3 to get the volume down to normal. */
            out_l += (low_fir_pas16(0, (double) pas16->pcm_buffer[0][c >> 1]) * mixer->pcm_l) / 3.0;
            out_r += (low_fir_pas16(1, (double) pas16->pcm_buffer[1][c >> 1]) * mixer->pcm_r) / 3.0;
        } else {
            out_l += (((double) pas16->pcm_buffer[0][c >> 1]) * mixer->pcm_l) / 3.0;
            out_r += (((double) pas16->pcm_buffer[1][c >> 1]) * mixer->pcm_r) / 3.0;
        }

        out_l *= mixer->master_l;
        out_r *= mixer->master_r;

        /* This is not exactly how one does bass/treble controls, but the end result is like it.
           A better implementation would reduce the CPU usage. */
        if (mixer->bass != 6) {
            bass_treble = lmc1982_bass_treble_4bits[mixer->bass];

            if (mixer->bass > 6) {
                out_l += (low_iir(0, 0, out_l) * bass_treble);
                out_r += (low_iir(0, 1, out_r) * bass_treble);
            } else if (mixer->bass < 6) {
                out_l = (out_l *bass_treble + low_cut_iir(0, 0, out_l) * (1.0 - bass_treble));
                out_r = (out_r *bass_treble + low_cut_iir(0, 1, out_r) * (1.0 - bass_treble));
            }
        }

        if (mixer->treble != 6) {
            bass_treble = lmc1982_bass_treble_4bits[mixer->treble];

            if (mixer->treble > 6) {
                out_l += (high_iir(0, 0, out_l) * bass_treble);
                out_r += (high_iir(0, 1, out_r) * bass_treble);
            } else if (mixer->treble < 6) {
                out_l = (out_l *bass_treble + high_cut_iir(0, 0, out_l) * (1.0 - bass_treble));
                out_r = (out_r *bass_treble + high_cut_iir(0, 1, out_r) * (1.0 - bass_treble));
            }
        }

        buffer[c] += (int32_t) out_l;
        buffer[c + 1] += (int32_t) out_r;
    }

    pas16->pos = 0;
    pas16->dsp.pos = 0;
}

void
pas16_get_music_buffer(int32_t *buffer, int len, void *priv)
{
    const pas16_t *      pas16   = (const pas16_t *) priv;
    const mv508_mixer_t *mixer   = &pas16->mv508_mixer;
    const int32_t *      opl_buf = pas16->opl.update(pas16->opl.priv);
    double               bass_treble;

    for (int c = 0; c < len * 2; c += 2) {
        double out_l = (((double) opl_buf[c]) * mixer->fm_l) * 0.7171630859375;
        double out_r = (((double) opl_buf[c + 1]) * mixer->fm_r) * 0.7171630859375;

        /* TODO: recording CD, Mic with AGC or line in. Note: mic volume does not affect recording. */
        out_l *= mixer->master_l;
        out_r *= mixer->master_r;

        /* This is not exactly how one does bass/treble controls, but the end result is like it.
           A better implementation would reduce the CPU usage. */
        if (mixer->bass != 6) {
            bass_treble = lmc1982_bass_treble_4bits[mixer->bass];

            if (mixer->bass > 6) {
                out_l += (low_iir(1, 0, out_l) * bass_treble);
                out_r += (low_iir(1, 1, out_r) * bass_treble);
            } else if (mixer->bass < 6) {
                out_l = (out_l *bass_treble + low_cut_iir(1, 0, out_l) * (1.0 - bass_treble));
                out_r = (out_r *bass_treble + low_cut_iir(1, 1, out_r) * (1.0 - bass_treble));
            }
        }

        if (mixer->treble != 6) {
            bass_treble = lmc1982_bass_treble_4bits[mixer->treble];

            if (mixer->treble > 6) {
                out_l += (high_iir(1, 0, out_l) * bass_treble);
                out_r += (high_iir(1, 1, out_r) * bass_treble);
            } else if (mixer->treble < 6) {
                out_l = (out_l *bass_treble + high_cut_iir(1, 0, out_l) * (1.0 - bass_treble));
                out_r = (out_r *bass_treble + high_cut_iir(1, 1, out_r) * (1.0 - bass_treble));
            }
        }

        buffer[c] += (int32_t) out_l;
        buffer[c + 1] += (int32_t) out_r;
    }

    pas16->opl.reset_buffer(pas16->opl.priv);
}

void
pas16_filter_cd_audio(int channel, double *buffer, void *priv)
{
    const pas16_t *      pas16  = (const pas16_t *) priv;
    const mv508_mixer_t *mixer  = &pas16->mv508_mixer;
    const double         cd     = channel ? mixer->cd_r : mixer->cd_l;
    const double         master = channel ? mixer->master_r : mixer->master_l;
    const int32_t        bass   = mixer->bass;
    const int32_t        treble = mixer->treble;
    double               c      = (((*buffer) * cd) / 3.0) * master;
    double               bass_treble;

    /* This is not exactly how one does bass/treble controls, but the end result is like it.
       A better implementation would reduce the CPU usage. */
    if (bass != 6) {
        bass_treble = lmc1982_bass_treble_4bits[bass];

        if (bass > 6)
            c += (low_iir(2, channel, c) * bass_treble);
        else
            c = (c * bass_treble + low_cut_iir(2, channel, c) * (1.0 - bass_treble));
    }

    if (treble != 6) {
        bass_treble = lmc1982_bass_treble_4bits[treble];

        if (treble > 6)
            c += (high_iir(2, channel, c) * bass_treble);
        else
            c = (c * bass_treble + high_cut_iir(2, channel, c) * (1.0 - bass_treble));
    }

    *buffer = c;
}

void
pas16_filter_pc_speaker(int channel, double *buffer, void *priv)
{
    const pas16_t *      pas16  = (const pas16_t *) priv;
    const mv508_mixer_t *mixer  = &pas16->mv508_mixer;
    const double         spk    = channel ? mixer->speaker_r : mixer->speaker_l;
    const double         master = channel ? mixer->master_r : mixer->master_l;
    const int32_t        bass   = mixer->bass;
    const int32_t        treble = mixer->treble;
    double               c      = (((*buffer) * spk) / 3.0) * master;
    double               bass_treble;

    /* This is not exactly how one does bass/treble controls, but the end result is like it.
       A better implementation would reduce the CPU usage. */
    if (bass != 6) {
        bass_treble = lmc1982_bass_treble_4bits[bass];

        if (bass > 6)
            c += (low_iir(3, channel, c) * bass_treble);
        else
            c = (c * bass_treble + low_cut_iir(3, channel, c) * (1.0 - bass_treble));
    }

    if (treble != 6) {
        bass_treble = lmc1982_bass_treble_4bits[treble];

        if (treble > 6)
            c += (high_iir(3, channel, c) * bass_treble);
        else
            c = (c * bass_treble + high_cut_iir(3, channel, c) * (1.0 - bass_treble));
    }

    *buffer = c;
}

static void
pas16_speed_changed(void *priv)
{
    pas16_change_pit_clock_speed(priv);
}

static void *
pas16_init(const device_t *info)
{
    pas16_t *pas16 = calloc(1, sizeof(pas16_t));

    if (pas16_next > 3) {
        fatal("Attempting to add a Pro Audio Spectrum instance beyond the maximum amount\n");

        free(pas16);
        return NULL;
    }

    pas16->type = info->local & 0xff;
    pas16->has_scsi = (!pas16->type) || (pas16->type == 0x0f);
    fm_driver_get(FM_YMF262, &pas16->opl);
    sb_dsp_set_real_opl(&pas16->dsp, 1);
    sb_dsp_init(&pas16->dsp, SB_DSP_201, SB_SUBTYPE_DEFAULT, pas16);
    pas16->mpu = (mpu_t *) calloc(1, sizeof(mpu_t));
    mpu401_init(pas16->mpu, 0, 0, M_UART, device_get_config_int("receive_input401"));
    sb_dsp_set_mpu(&pas16->dsp, pas16->mpu);

    pas16->sb_compat_base = 0x0000;

    io_sethandler(0x9a01, 0x0001, NULL, NULL, NULL, pas16_out_base, NULL, NULL, pas16);
    pas16->this_id = 0xbc + pas16_next;

    if (pas16->has_scsi) {
        pas16->scsi = device_add(&scsi_pas_device);
        timer_add(&pas16->scsi->timer, pas16_scsi_callback, pas16, 0);
        timer_add(&pas16->scsi_timer, pas16_timeout_callback, pas16, 0);
        other_scsi_present++;
    }

    pas16->pit = device_add(&i8254_ext_io_fast_device);
    pas16_reset(pas16);
    pas16->pit->dev_priv = pas16;
    pas16->irq = pas16->type ? 10 : 5;
    pas16->io_conf_3 = pas16->type ? 0x07 : 0x04;
    if (pas16->has_scsi) {
        pas16->scsi_irq = pas16->type ? 11 : 7;
        pas16->io_conf_3 |= (pas16->type ? 0x80 : 0x60);
        ncr5380_set_irq(&pas16->scsi->ncr, pas16->scsi_irq);
    }
    pas16->dma = 3;
    for (uint8_t i = 0; i < 3; i++)
        pitf_ctr_set_gate(pas16->pit, i, 0);

    pitf_ctr_set_out_func(pas16->pit, 0, pas16_pit_timer0);
    pitf_ctr_set_out_func(pas16->pit, 1, pas16_pit_timer1);
    pitf_ctr_set_using_timer(pas16->pit, 0, 1);
    pitf_ctr_set_using_timer(pas16->pit, 1, 0);
    pitf_ctr_set_using_timer(pas16->pit, 2, 0);

    if (pas16->type) {
        sound_add_handler(pas16_get_buffer, pas16);
        music_add_handler(pas16_get_music_buffer, pas16);
        sound_set_cd_audio_filter(pas16_filter_cd_audio, pas16);
        if (device_get_config_int("control_pc_speaker"))
            sound_set_pc_speaker_filter(pas16_filter_pc_speaker, pas16);
    } else {
        sound_add_handler(pasplus_get_buffer, pas16);
        music_add_handler(pasplus_get_music_buffer, pas16);
        sound_set_cd_audio_filter(pasplus_filter_cd_audio, pas16);
        if (device_get_config_int("control_pc_speaker"))
            sound_set_pc_speaker_filter(pasplus_filter_pc_speaker, pas16);
    }

    if (device_get_config_int("receive_input"))
        midi_in_handler(1, pas16_input_msg, pas16_input_sysex, pas16);

    for (uint8_t i = 0; i < 16; i++) {
        if (i < 6)
            lmc1982_bass_treble_4bits[i] = pow(10.0, (-((double) (12 - (i << 1))) / 10.0));
        else if (i == 6)
            lmc1982_bass_treble_4bits[i] = 0.0;
        else if ((i > 6) && (i <= 12))
            lmc1982_bass_treble_4bits[i] = 1.0 - pow(10.0, ((double) ((i - 6) << 1) / 10.0));
        else
            lmc1982_bass_treble_4bits[i] = 1.0 - pow(10.0, 1.2);
    }

    pas16_next++;

    return pas16;
}

static void
pas16_close(void *priv)
{
    pas16_t *pas16 = (pas16_t *) priv;

    free(pas16);

    pas16_next = 0;
}

static const device_config_t pas16_config[] = {
    {
        .name           = "control_pc_speaker",
        .description    = "Control PC speaker",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
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
};

const device_t pasplus_device = {
    .name          = "Pro Audio Spectrum Plus",
    .internal_name = "pasplus",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = pas16_init,
    .close         = pas16_close,
    .reset         = pas16_reset,
    .available     = NULL,
    .speed_changed = pas16_speed_changed,
    .force_redraw  = NULL,
    .config        = pas16_config
};

const device_t pas16_device = {
    .name          = "Pro Audio Spectrum 16",
    .internal_name = "pas16",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0x0f,
    .init          = pas16_init,
    .close         = pas16_close,
    .reset         = pas16_reset,
    .available     = NULL,
    .speed_changed = pas16_speed_changed,
    .force_redraw  = NULL,
    .config        = pas16_config
};

const device_t pas16d_device = {
    .name          = "Pro Audio Spectrum 16D",
    .internal_name = "pas16d",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0x0c,
    .init          = pas16_init,
    .close         = pas16_close,
    .reset         = pas16_reset,
    .available     = NULL,
    .speed_changed = pas16_speed_changed,
    .force_redraw  = NULL,
    .config        = pas16_config
};
