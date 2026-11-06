/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          USB speaker: 16-bit stereo PCM on an adaptive isochronous
 *          endpoint, resampled to the mixer rate.
 *
 *          Written from:
 *          - USB Device Class Definition for Audio Devices, Release 1.0,
 *            March 18, 1998 ("Audio 1.0").
 *          - USB Device Class Definition for Audio Data Formats,
 *            Release 1.0, March 18, 1998 ("Audio Formats 1.0").
 *          - USB Device Class Definition for Terminal Types, Release 1.0,
 *            March 18, 1998.
 *          - Universal Serial Bus Specification, Revision 1.1,
 *            September 23, 1998.
 *          - Universal Host Controller Interface (UHCI) Design Guide,
 *            Revision 1.1, March 1996.
 *
 * Authors: skiretic, <skiretic@proton.me>
 *
 *          Copyright 2026 skiretic.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/sound.h>
#include <86box/usb.h>
#include <86box/usb_dev.h>
#include <86box/plat_unused.h>

#define AUD_IF_CONTROL 0
#define AUD_IF_STREAM  1
#define AUD_EP_OUT     1

/* Entity IDs: USB streaming -> Feature Unit -> Speaker. */
#define AUD_IT_ID 1
#define AUD_FU_ID 2
#define AUD_OT_ID 3

/* Request codes and control selectors (Audio 1.0 Table A-9, p. 101 /
   PDF 101; Tables A-11 and A-19, p. 102-104 / PDF 102-104). */
#define AUD_SET_CUR 0x01
#define AUD_GET_CUR 0x81
#define AUD_GET_MIN 0x82
#define AUD_GET_MAX 0x83
#define AUD_GET_RES 0x84

#define AUD_MUTE_CONTROL    0x01
#define AUD_VOLUME_CONTROL  0x02
#define AUD_SAMPLING_FREQ   0x01

/* Volume in 1/256 dB; 8000h means silence and is valid for CUR only
   (Audio 1.0 s5.2.2.4.3.2, p. 76-77 / PDF 76-77). This device offers
   -60 dB to 0 dB in 1 dB steps. */
#define AUD_VOL_MIN (-60 * 256)
#define AUD_VOL_MAX 0
#define AUD_VOL_RES 256
#define AUD_VOL_SILENCE (-32768)

/* Queue in source frames; playback starts once 30 ms is in, and at most
   200 ms is kept. */
#define AUD_QUEUE   16384
#define AUD_START_MS 30
#define AUD_MAX_MS   200

/* An adaptive sink takes its rate from the data it receives (USB 1.1
   s5.10.4.1.3, p. 63 / PDF 79). Here the resampling step follows the queue
   fill as it drifts from the fill playback started at. A fill off by the
   whole target would move the step 5%; the step moves at most 1%, which
   covers both UHCI's frame length range (11936 to 12063 bit times, UHCI 1.1
   s2.1.6, p. 15 / PDF 21) and the +/-1000 ppm an adaptive endpoint must
   tolerate (Audio Formats 1.0 s2.2.5, p. 11 / PDF 11). The fill is
   smoothed over about 10 mixer calls. */
#define AUD_ADAPT_GAIN 0.05
#define AUD_ADAPT_MAX  0.01
#define AUD_FILL_ALPHA 0.1

static const uint32_t aud_rates[] = { 8000, 11025, 16000, 22050, 32000, 44100, 48000 };
#define AUD_N_RATES (sizeof(aud_rates) / sizeof(aud_rates[0]))

/* 49 frames of 4 bytes. At 48 kHz a 1 ms packet may hold one sample more
   than the average of 48 (Audio Formats 1.0 s2.2.1, p. 9 / PDF 9). */
#define AUD_MPS 196

static const uint8_t aud_dev_desc[18] = {
    18, 0x01,   /* bLength, DEVICE */
    0x10, 0x01, /* bcdUSB 1.10 */
    0x00, 0x00, 0x00, /* class in the interfaces */
    64,         /* bMaxPacketSize0 */
    0x00, 0x00, /* idVendor */
    0x04, 0x00, /* idProduct */
    0x00, 0x01, /* bcdDevice */
    1, 2, 0,    /* iManufacturer, iProduct, iSerialNumber */
    1           /* bNumConfigurations */
};

#define AUD_AC_CS_LEN (9 + 12 + 10 + 9)
#define AUD_TYPE_I_LEN (8 + 3 * AUD_N_RATES)
#define AUD_CFG_LEN (9 + 9 + AUD_AC_CS_LEN + 9 + 9 + 7 + AUD_TYPE_I_LEN + 9 + 7)

#define AUD_RATE(r) ((r) & 0xff), (((r) >> 8) & 0xff), (((r) >> 16) & 0xff)

static const uint8_t aud_cfg_desc[AUD_CFG_LEN] = {
    /* Configuration */
    9, 0x02, AUD_CFG_LEN & 0xff, AUD_CFG_LEN >> 8, 2, 1, 0,
    0x80, /* bus powered */
    50,   /* 100 mA */

    /* AudioControl interface (Audio 1.0 s4.3.1, p. 36 / PDF 36) */
    9, 0x04, AUD_IF_CONTROL, 0, 0, 0x01, 0x01, 0x00, 0,
    /* Header: ADC 1.00, one streaming interface (Table 4-2, p. 37 / PDF 37) */
    9, 0x24, 0x01, 0x00, 0x01, AUD_AC_CS_LEN, 0x00, 1, AUD_IF_STREAM,
    /* Input Terminal: USB streaming (0101h), 2 channels, left and right
       (Table 4-3, p. 39 / PDF 39; Terminal Types 1.0 s2.1, p. 7 / PDF 7) */
    12, 0x24, 0x02, AUD_IT_ID, 0x01, 0x01, 0, 2, 0x03, 0x00, 0, 0,
    /* Feature Unit: master Mute and Volume, none per channel (Table 4-7,
       p. 43 / PDF 43) */
    10, 0x24, 0x06, AUD_FU_ID, AUD_IT_ID, 1, 0x03, 0x00, 0x00, 0,
    /* Output Terminal: Speaker (0301h) (Table 4-4, p. 40 / PDF 40; Terminal
       Types 1.0 s2.3, p. 8 / PDF 8) */
    9, 0x24, 0x03, AUD_OT_ID, 0x01, 0x03, 0, AUD_FU_ID, 0,

    /* AudioStreaming interface, setting 0: no endpoint, so zero bandwidth
       (Audio 1.0 s3.7.2, p. 31 / PDF 31) */
    9, 0x04, AUD_IF_STREAM, 0, 0, 0x01, 0x02, 0x00, 0,
    /* Setting 1: one isochronous endpoint */
    9, 0x04, AUD_IF_STREAM, 1, 1, 0x01, 0x02, 0x00, 0,
    /* AS_GENERAL: from the Input Terminal, one frame of delay, PCM (Table 4-19,
       p. 60 / PDF 60) */
    7, 0x24, 0x01, AUD_IT_ID, 1, 0x01, 0x00,
    /* Type I: 2 channels, 2-byte subframes, 16 bits, discrete rates (Audio
       Formats 1.0 s2.2.5, p. 10-11 / PDF 10-11) */
    AUD_TYPE_I_LEN, 0x24, 0x02, 0x01, 2, 2, 16, AUD_N_RATES,
    AUD_RATE(8000), AUD_RATE(11025), AUD_RATE(16000), AUD_RATE(22050),
    AUD_RATE(32000), AUD_RATE(44100), AUD_RATE(48000),
    /* Endpoint 1 OUT, isochronous adaptive, 1 ms (Table 4-20, p. 61 / PDF 61) */
    9, 0x05, AUD_EP_OUT, 0x09, AUD_MPS & 0xff, AUD_MPS >> 8, 1, 0, 0,
    /* EP_GENERAL: Sampling Frequency Control (Table 4-21, p. 62 / PDF 62) */
    7, 0x25, 0x01, 0x01, 0, 0, 0
};

static const char *const aud_strings[] = {
    "86Box",
    "USB Speaker"
};

typedef struct usb_audio_t {
    usb_dev_t usb;
    uint32_t  rate;
    int16_t   volume; /* 1/256 dB */
    uint8_t   mute;
    int       streaming;
    int       playing;
    int16_t   queue[AUD_QUEUE][2];
    int       q_head;
    int       q_len;
    double    pos;         /* position between queue[q_head] and the next frame */
    int       fill_set;    /* fill_avg and fill_target hold for this playback */
    double    fill_avg;    /* queue fill at each mixer call, smoothed */
    double    fill_target; /* the fill playback started at */
} usb_audio_t;

static void
usb_audio_stop(usb_audio_t *dev)
{
    dev->q_head   = 0;
    dev->q_len    = 0;
    dev->pos      = 0.0;
    dev->playing  = 0;
    dev->fill_set = 0;
}

static uint32_t
usb_audio_closest_rate(uint32_t hz)
{
    uint32_t best = aud_rates[0];

    for (unsigned i = 1; i < AUD_N_RATES; i++) {
        if (abs((int) aud_rates[i] - (int) hz) < abs((int) best - (int) hz))
            best = aud_rates[i];
    }

    return best;
}

static void
usb_audio_put16(uint8_t *p, int v)
{
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
}

/* Mute and Volume of the Feature Unit, which exist on the master channel
   (0) only. A request may name channel 0 (first form) or FFh, all channels
   (second form); with one Control of each type, both forms address the same
   Control and carry the same parameter block (Audio 1.0 s5.2.2.4,
   p. 74-75 / PDF 74-75). Any other channel or selector is stalled. */
static int
usb_audio_fu_request(usb_audio_t *dev, const uint8_t *setup, uint8_t *data)
{
    int      selector = setup[3];
    int      channel  = setup[2];
    uint16_t wlength  = setup[6] | (setup[7] << 8);
    int      v;

    if ((channel != 0) && (channel != 0xff))
        return USB_DATA_STALL;

    switch (selector) {
        case AUD_MUTE_CONTROL:
            if (setup[1] == AUD_GET_CUR) {
                data[0] = dev->mute;
                return 1;
            }
            /* Mute has CUR only. One byte in either form (Tables 5-16 and
               5-17, p. 75-76 / PDF 75-76). */
            if ((setup[1] == AUD_SET_CUR) && (wlength == 1)) {
                dev->mute = !!data[0];
                return 0;
            }
            return USB_DATA_STALL;

        case AUD_VOLUME_CONTROL:
            switch (setup[1]) {
                case AUD_GET_CUR:
                    usb_audio_put16(data, dev->volume);
                    return 2;
                case AUD_GET_MIN:
                    usb_audio_put16(data, AUD_VOL_MIN);
                    return 2;
                case AUD_GET_MAX:
                    usb_audio_put16(data, AUD_VOL_MAX);
                    return 2;
                case AUD_GET_RES:
                    usb_audio_put16(data, AUD_VOL_RES);
                    return 2;
                case AUD_SET_CUR:
                    /* Two bytes in either form (Tables 5-18 and 5-19,
                       p. 76-77 / PDF 76-77). */
                    if (wlength != 2)
                        return USB_DATA_STALL;
                    /* 8000h (silence) is kept as is. Any other value is
                       rounded to the closest available setting, which GET_CUR
                       then reports (s5.2.2.4.3.2, p. 76 / PDF 76). */
                    v = (int16_t) (data[0] | (data[1] << 8));
                    if (v == AUD_VOL_SILENCE) {
                        dev->volume = v;
                        return 0;
                    }
                    v = ((v + ((v < 0) ? -(AUD_VOL_RES / 2) : (AUD_VOL_RES / 2))) / AUD_VOL_RES) * AUD_VOL_RES;
                    if (v < AUD_VOL_MIN)
                        v = AUD_VOL_MIN;
                    if (v > AUD_VOL_MAX)
                        v = AUD_VOL_MAX;
                    dev->volume = v;
                    return 0;
                default:
                    return USB_DATA_STALL;
            }

        default:
            return USB_DATA_STALL;
    }
}

static int
usb_audio_control(usb_dev_t *usb, const uint8_t *setup, uint8_t *data)
{
    usb_audio_t *dev     = (usb_audio_t *) usb->priv;
    uint16_t     windex  = setup[4] | (setup[5] << 8);
    uint16_t     wlength = setup[6] | (setup[7] << 8);

    /* Every Get request is device-to-host and every Set request
       host-to-device (Audio 1.0 Tables 5-1 and 5-2, p. 66-67 / PDF 66-67).
       The request codes carry the same split in bit 7, so the two bits must
       agree; a Set sent device-to-host would otherwise act on a data buffer
       the host never filled. An unsupported request is stalled (s5.2, p. 65
       / PDF 65). */
    if ((setup[0] ^ setup[1]) & 0x80)
        return USB_DATA_STALL;

    /* Class requests to the AudioControl interface name the entity in the
       high byte of wIndex. Only the Feature Unit has Controls; a request to
       any other entity is stalled (s5.2.1.1-s5.2.1.2, p. 66-67 / PDF 66-67). */
    if (((setup[0] == 0x21) || (setup[0] == 0xa1)) && ((windex & 0xff) == AUD_IF_CONTROL)) {
        if ((windex >> 8) == AUD_FU_ID)
            return usb_audio_fu_request(dev, setup, data);
        return USB_DATA_STALL;
    }

    /* Sampling Frequency Control of the streaming endpoint: a rate not in
       the descriptor's list is rounded to the closest listed rate, which
       GET_CUR then reports (s5.2.3.2.3.1, p. 96 / PDF 96). */
    if (((setup[0] == 0x22) || (setup[0] == 0xa2)) && (windex == AUD_EP_OUT) && (setup[3] == AUD_SAMPLING_FREQ) &&
        (setup[2] == 0)) {
        if (setup[1] == AUD_GET_CUR) {
            data[0] = dev->rate & 0xff;
            data[1] = (dev->rate >> 8) & 0xff;
            data[2] = (dev->rate >> 16) & 0xff;
            return 3;
        }
        if ((setup[1] == AUD_SET_CUR) && (wlength >= 3)) {
            dev->rate     = usb_audio_closest_rate(data[0] | (data[1] << 8) | (data[2] << 16));
            dev->pos      = 0.0;
            dev->fill_set = 0;
            return 0;
        }
    }

    return USB_DATA_STALL;
}

/* One isochronous packet of 16-bit stereo frames; the oldest frames give way
   when the queue is full, so latency stays bounded. */
static int
usb_audio_data_out(usb_dev_t *usb, int ep, const uint8_t *buf, int len)
{
    usb_audio_t *dev = (usb_audio_t *) usb->priv;
    int          max = (int) ((dev->rate * AUD_MAX_MS) / 1000);

    if ((ep != AUD_EP_OUT) || !dev->streaming)
        return 0;

    if (max > AUD_QUEUE)
        max = AUD_QUEUE;

    for (int i = 0; (i + 3) < len; i += 4) {
        int tail;

        if (dev->q_len >= max) {
            dev->q_head = (dev->q_head + 1) % AUD_QUEUE;
            dev->q_len--;
        }
        tail                  = (dev->q_head + dev->q_len) % AUD_QUEUE;
        dev->queue[tail][0]   = (int16_t) (buf[i] | (buf[i + 1] << 8));
        dev->queue[tail][1]   = (int16_t) (buf[i + 2] | (buf[i + 3] << 8));
        dev->q_len++;
    }

    if (!dev->playing && (dev->q_len >= (int) ((dev->rate * AUD_START_MS) / 1000)))
        dev->playing = 1;

    return 0;
}

/* The mixer's 48 kHz frames, linearly interpolated from the stream rate. */
static void
usb_audio_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    usb_audio_t *dev  = (usb_audio_t *) priv;
    double       step = (double) dev->rate / (double) SOUND_FREQ;
    double       gain = (dev->mute || (dev->volume == AUD_VOL_SILENCE)) ? 0.0 : pow(10.0, (double) dev->volume / (256.0 * 20.0));
    double       adj;

    if (!dev->playing)
        return;

    if (!dev->fill_set) {
        dev->fill_avg    = dev->q_len;
        dev->fill_target = dev->q_len;
        dev->fill_set    = 1;
    } else
        dev->fill_avg += (dev->q_len - dev->fill_avg) * AUD_FILL_ALPHA;
    adj = AUD_ADAPT_GAIN * (dev->fill_avg - dev->fill_target) / dev->fill_target;
    if (adj > AUD_ADAPT_MAX)
        adj = AUD_ADAPT_MAX;
    if (adj < -AUD_ADAPT_MAX)
        adj = -AUD_ADAPT_MAX;
    step *= 1.0 + adj;

    for (int c = 0; c < len; c++) {
        const int16_t *a;
        const int16_t *b;

        /* Two frames are needed to interpolate; running dry means waiting
           for 30 ms again. */
        if (dev->q_len < 2) {
            dev->playing  = 0;
            dev->pos      = 0.0;
            dev->fill_set = 0;
            break;
        }

        a = dev->queue[dev->q_head];
        b = dev->queue[(dev->q_head + 1) % AUD_QUEUE];
        buffer[c * 2] += (int32_t) lrint(((double) a[0] + ((double) (b[0] - a[0]) * dev->pos)) * gain);
        buffer[c * 2 + 1] += (int32_t) lrint(((double) a[1] + ((double) (b[1] - a[1]) * dev->pos)) * gain);

        dev->pos += step;
        while ((dev->pos >= 1.0) && (dev->q_len > 1)) {
            dev->pos -= 1.0;
            dev->q_head = (dev->q_head + 1) % AUD_QUEUE;
            dev->q_len--;
        }
    }
}

static void
usb_audio_set_interface(usb_dev_t *usb, int intf, int alt)
{
    usb_audio_t *dev = (usb_audio_t *) usb->priv;

    if (intf != AUD_IF_STREAM)
        return;

    /* Any SET_INTERFACE empties the queue; setting 0 has no endpoint, so the
       stream also stops. */
    usb_audio_stop(dev);
    dev->streaming = (alt == 1);
}

static void
usb_audio_reset(usb_dev_t *usb)
{
    usb_audio_t *dev = (usb_audio_t *) usb->priv;

    usb_audio_stop(dev);
    dev->streaming = 0;
}

static void
usb_audio_configured(usb_dev_t *usb, UNUSED(int config))
{
    usb_audio_reset(usb);
}

static const usb_dev_ops_t usb_audio_ops = {
    .control       = usb_audio_control,
    .data_in       = NULL,
    .data_out      = usb_audio_data_out,
    .reset         = usb_audio_reset,
    .configured    = usb_audio_configured,
    .set_interface = usb_audio_set_interface
};

static void *
usb_audio_init(const device_t *info)
{
    usb_audio_t *dev = (usb_audio_t *) calloc(1, sizeof(usb_audio_t));

    dev->rate   = 48000;
    dev->volume = 0;
    dev->mute   = 0;

    dev->usb.name      = "USB Speaker";
    dev->usb.low_speed = 0;
    dev->usb.dev_desc  = aud_dev_desc;
    dev->usb.cfg_desc  = aud_cfg_desc;
    dev->usb.cfg_len   = AUD_CFG_LEN;
    dev->usb.strings   = aud_strings;
    dev->usb.n_strings = 2;
    dev->usb.ops       = &usb_audio_ops;
    dev->usb.priv      = dev;
    usb_dev_init(&dev->usb);

    sound_add_handler(usb_audio_get_buffer, dev);

    usb_dev_add(&dev->usb, info);

    return dev;
}

static void
usb_audio_close(void *priv)
{
    usb_audio_t *dev = (usb_audio_t *) priv;

    usb_dev_remove(&dev->usb);
    free(dev);
}

const device_t usb_speaker_device = {
    .name          = "USB Speaker",
    .internal_name = "usb_speaker",
    .flags         = DEVICE_USB,
    .local         = 0,
    .init          = usb_audio_init,
    .close         = usb_audio_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
