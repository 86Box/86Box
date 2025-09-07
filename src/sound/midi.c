/*
 * 86Box     A hypervisor and IBM PC system emulator that specializes in
 *           running old operating systems and software designed for IBM
 *           PC systems and compatibles from 1981 through fairly recent
 *           system designs based on the PCI bus.
 *
 *           This file is part of the 86Box distribution.
 *
 *           MIDI device core module.
 *
 *
 *
 * Authors:  Miran Grca, <mgrca8@gmail.com>
 *           Bit,
 *           DOSBox Team,
 *
 *           Copyright 2016-2020 Miran Grca.
 *           Copyright 2016-2020 Bit.
 *           Copyright 2008-2020 DOSBox Team.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/midi.h>
#include <86box/plat.h>

int        midi_output_device_current = 0;
static int midi_output_device_last    = 0;
int        midi_input_device_current  = 0;
static int midi_input_device_last     = 0;

midi_t *midi_out = NULL;
midi_t *midi_in  = NULL;

midi_in_handler_t *mih_first = NULL;
midi_in_handler_t *mih_last = NULL;
midi_in_handler_t *mih_cur = NULL;

uint8_t MIDI_InSysexBuf[SYSEX_SIZE];

uint8_t MIDI_evt_len[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x00 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x10 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x20 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x30 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x40 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x50 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x60 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x70 */

    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* 0x80 */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* 0x90 */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* 0xa0 */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* 0xb0 */

    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 0xc0 */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 0xd0 */

    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* 0xe0 */

    0, 2, 3, 2, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0 /* 0xf0 */
};

typedef struct
{
    const device_t *device;
} MIDI_OUT_DEVICE, MIDI_IN_DEVICE;

static const MIDI_OUT_DEVICE midi_out_devices[] = {
    // clang-format off
    { &device_none          },
#ifdef USE_FLUIDSYNTH
    { &fluidsynth_device    },
#endif /* USE_FLUIDSYNTH */
#ifdef USE_MUNT
    { &mt32_old_device      },
    { &mt32_new_device      },
    { &cm32l_device         },
    { &cm32ln_device        },
#endif /*USE_MUNT */
#ifdef USE_OPL4ML
    { &opl4_midi_device     },
#endif /* USE_OPL4ML */
#ifdef USE_RTMIDI
    { &rtmidi_output_device },
#endif /* USE_RTMIDI */
    { NULL                  }
    // clang-format on
};

static const MIDI_IN_DEVICE midi_in_devices[] = {
    // clang-format off
    { &device_none         },
#ifdef USE_RTMIDI
    { &rtmidi_input_device },
#endif /* USE_RTMIDI */
    { NULL                 }
    // clang-format on
};

int
midi_out_device_available(int card)
{
    if (midi_out_devices[card].device)
        return device_available(midi_out_devices[card].device);

    return 1;
}

const device_t *
midi_out_device_getdevice(int card)
{
    return midi_out_devices[card].device;
}

int
midi_out_device_has_config(int card)
{
    if (!midi_out_devices[card].device)
        return 0;
    return midi_out_devices[card].device->config ? 1 : 0;
}

const char *
midi_out_device_get_internal_name(int card)
{
    return device_get_internal_name(midi_out_devices[card].device);
}

int
midi_out_device_get_from_internal_name(char *s)
{
    int c = 0;

    while (midi_out_devices[c].device != NULL) {
        if (!strcmp(midi_out_devices[c].device->internal_name, s))
            return c;
        c++;
    }

    return 0;
}

void
midi_out_device_init(void)
{
    if ((midi_output_device_current > 0) && midi_out_devices[midi_output_device_current].device)
        device_add(midi_out_devices[midi_output_device_current].device);
    midi_output_device_last = midi_output_device_current;
}

void
midi_out_init(midi_device_t *device)
{
    midi_out = (midi_t *) calloc(1, sizeof(midi_t));

    midi_out->m_out_device = device;
}

void
midi_in_init(midi_device_t *device, midi_t **mididev)
{
    *mididev = (midi_t *) calloc(1, sizeof(midi_t));

    (*mididev)->m_in_device = device;
}

void
midi_out_close(void)
{
    if (midi_out && midi_out->m_out_device) {
        free(midi_out->m_out_device);
        midi_out->m_out_device = NULL;
    }

    if (midi_out) {
        free(midi_out);
        midi_out = NULL;
    }
}

void
midi_in_close(void)
{
    if (midi_in && midi_in->m_in_device) {
        free(midi_in->m_in_device);
        midi_in->m_in_device = NULL;
    }

    if (midi_in) {
        free(midi_in);
        midi_in = NULL;
    }
}

void
midi_poll(void)
{
    if (midi_out && midi_out->m_out_device && midi_out->m_out_device->poll)
        midi_out->m_out_device->poll();
}

void
play_msg(uint8_t *msg)
{
    if (midi_out->m_out_device->play_msg)
        midi_out->m_out_device->play_msg(msg);
}

void
play_sysex(uint8_t *sysex, unsigned int len)
{
    if (midi_out->m_out_device->play_sysex)
        midi_out->m_out_device->play_sysex(sysex, len);
}

int
midi_in_device_available(int card)
{
    if (midi_in_devices[card].device)
        return device_available(midi_in_devices[card].device);

    return 1;
}

const device_t *
midi_in_device_getdevice(int card)
{
    return midi_in_devices[card].device;
}

int
midi_in_device_has_config(int card)
{
    if (!midi_in_devices[card].device)
        return 0;
    return midi_in_devices[card].device->config ? 1 : 0;
}

const char *
midi_in_device_get_internal_name(int card)
{
    return device_get_internal_name(midi_in_devices[card].device);
}

int
midi_in_device_get_from_internal_name(char *s)
{
    int c = 0;

    while (midi_in_devices[c].device != NULL) {
        if (!strcmp(midi_in_devices[c].device->internal_name, s))
            return c;
        c++;
    }

    return 0;
}

void
midi_in_device_init(void)
{
    if ((midi_input_device_current > 0) && midi_in_devices[midi_input_device_current].device)
        device_add(midi_in_devices[midi_input_device_current].device);
    midi_input_device_last = midi_input_device_current;
}

void
midi_raw_out_rt_byte(uint8_t val)
{
    if (!midi_in)
        return;

    if (!midi_in->midi_realtime)
        return;

    if (!midi_in->midi_clockout && (val == 0xf8))
        return;

    midi_in->midi_cmd_r = val << 24;
    /* pclog("Play RT Byte msg\n"); */
    play_msg((uint8_t *) &midi_in->midi_cmd_r);
}

void
midi_raw_out_thru_rt_byte(uint8_t val)
{
    if (midi_in && midi_in->thruchan)
        midi_raw_out_rt_byte(val);
}

void
midi_raw_out_byte(uint8_t val)
{
    uint32_t passed_ticks;

    if (!midi_out || !midi_out->m_out_device)
        return;

    if (midi_out->m_out_device->write && midi_out->m_out_device->write(val))
        return;

    if (midi_out->midi_sysex_start) {
        passed_ticks = plat_get_ticks() - midi_out->midi_sysex_start;
        if (passed_ticks < midi_out->midi_sysex_delay)
            plat_delay_ms(midi_out->midi_sysex_delay - passed_ticks);
    }

    /* Test for a realtime MIDI message */
    if (val >= 0xf8) {
        midi_out->midi_rt_buf[0] = val;
        play_msg(midi_out->midi_rt_buf);
        return;
    }

    /* Test for a active sysex transfer */
    if (midi_out->midi_status == 0xf0) {
        if (!(val & 0x80)) {
            if (midi_out->midi_pos < (SYSEX_SIZE - 1))
                midi_out->midi_sysex_data[midi_out->midi_pos++] = val;
            return;
        } else {
            midi_out->midi_sysex_data[midi_out->midi_pos++] = 0xf7;

            if ((midi_out->midi_sysex_start) && (midi_out->midi_pos >= 4) && (midi_out->midi_pos <= 9) && (midi_out->midi_sysex_data[1] == 0x41) && (midi_out->midi_sysex_data[3] == 0x16)) {
                /* pclog("MIDI: Skipping invalid MT-32 SysEx MIDI message\n"); */
            } else {
                play_sysex(midi_out->midi_sysex_data, midi_out->midi_pos);
                if (midi_out->midi_sysex_start) {
                    if (midi_out->midi_sysex_data[5] == 0x7f)
                        midi_out->midi_sysex_delay = 290; /* All parameters reset */
                    else if ((midi_out->midi_sysex_data[5] == 0x10) && (midi_out->midi_sysex_data[6] == 0x00) && (midi_out->midi_sysex_data[7] == 0x04))
                        midi_out->midi_sysex_delay = 145; /* Viking Child */
                    else if ((midi_out->midi_sysex_data[5] == 0x10) && (midi_out->midi_sysex_data[6] == 0x00) && (midi_out->midi_sysex_data[7] == 0x01))
                        midi_out->midi_sysex_delay = 30; /* Dark Sun 1 */
                    else
                        midi_out->midi_sysex_delay = (unsigned int) (((double) (midi_out->midi_pos) * 1.25) / 3.125) + 2;

                    midi_out->midi_sysex_start = plat_get_ticks();
                }
            }
        }
    }

    if (val & 0x80) {
        midi_out->midi_status  = val;
        midi_out->midi_cmd_pos = 0;
        midi_out->midi_cmd_len = MIDI_evt_len[val];
        if (midi_out->midi_status == 0xf0) {
            midi_out->midi_sysex_data[0] = 0xf0;
            midi_out->midi_pos           = 1;
        }
    }

    if (midi_out->midi_cmd_len) {
        midi_out->midi_cmd_buf[midi_out->midi_cmd_pos++] = val;
        if (midi_out->midi_cmd_pos >= midi_out->midi_cmd_len) {
            play_msg(midi_out->midi_cmd_buf);
            midi_out->midi_cmd_pos = 1;
        }
    }
}

void
midi_clear_buffer(void)
{
    if (!midi_out)
        return;

    midi_out->midi_pos     = 0;
    midi_out->midi_status  = 0x00;
    midi_out->midi_cmd_pos = 0;
    midi_out->midi_cmd_len = 0;
}

void
midi_in_handler(int set, void (*msg)(void *priv, uint8_t *msg, uint32_t len), int (*sysex)(void *priv, uint8_t *buffer, uint32_t len, int abort), void *priv)
{
    midi_in_handler_t *temp = NULL;
    midi_in_handler_t *next;

    if (set) {
        /* Add MIDI IN handler. */
        if ((mih_first == NULL) && (mih_last != NULL))
            fatal("Last MIDI IN handler present with no first MIDI IN handler\n");

        if ((mih_first != NULL) && (mih_last == NULL))
            fatal("First MIDI IN handler present with no last MIDI IN handler\n");

        temp = (midi_in_handler_t *) calloc(1, sizeof(midi_in_handler_t));
        temp->msg   = msg;
        temp->sysex = sysex;
        temp->priv  = priv;

        if (mih_last == NULL)
            mih_first = mih_last = temp;
        else {
            temp->prev = mih_last;
            mih_last   = temp;
        }
    } else if ((mih_first != NULL) && (mih_last != NULL)) {
        temp = mih_first;

        while (1) {
            if (temp == NULL)
                break;

            if ((temp->msg == msg) && (temp->sysex == sysex) && (temp->priv == priv)) {
                if (temp->prev != NULL)
                    temp->prev->next = temp->next;

                if (temp->next != NULL)
                    temp->next->prev = temp->prev;

                next = temp->next;

                if (temp == mih_first) {
                    mih_first = NULL;
                    if (next == NULL)
                        mih_last = NULL;
                }

                if (temp == mih_last)
                    mih_last = NULL;

                free(temp);
                temp = next;

                if (next == NULL)
                    break;
            }
        }
    }
}

void
midi_in_handlers_clear(void)
{
    midi_in_handler_t *temp = mih_first;
    midi_in_handler_t *next;

    while (1) {
        if (temp == NULL)
            break;

        next = temp->next;
        free(temp);

        temp = next;

        if (next == NULL)
            break;
    }

    mih_first = mih_last = NULL;
}

void
midi_in_msg(uint8_t *msg, uint32_t len)
{
    midi_in_handler_t *temp = mih_first;

    while (1) {
        if (temp == NULL)
            break;

        if (temp->msg)
            temp->msg(temp->priv, msg, len);

        temp = temp->next;

        if (temp == NULL)
            break;
    }
}

static void
midi_start_sysex(uint8_t *buffer, uint32_t len)
{
    midi_in_handler_t *temp = mih_first;

    while (1) {
        if (temp == NULL)
            break;

        temp->cnt = 5;
        temp->buf = buffer;
        temp->len = len;

        temp = temp->next;

        if (temp == NULL)
            break;
    }
}

/* Returns:
    0 = All handlers have returnd 0;
    1 = There are still handlers to go. */
static int
midi_do_sysex(void)
{
    midi_in_handler_t *temp = mih_first;
    int                ret;
    int                cnt_acc = 0;

    while (1) {
        if (temp == NULL)
            break;

        /* Do nothing if the handler has a zero count. */
        if ((temp->cnt > 0) || (temp->len > 0)) {
            ret = 0;
            if (temp->sysex) {
                if (temp->cnt == 0)
                    ret = temp->sysex(temp->priv, temp->buf, 0, 0);
                else
                    ret = temp->sysex(temp->priv, temp->buf, temp->len, 0);
            }

            /* If count is 0 and length is 0, then this is just a finishing
               call to temp->sysex(), so skip this entire block. */
            if (temp->cnt > 0) {
                if (ret) {
                    /* Decrease or reset the counter. */
                    if (temp->len == ret)
                        temp->cnt--;
                    else
                        temp->cnt = 5;

                    /* Advance the buffer pointer and remember the
                       remaining length. */
                    temp->buf += (temp->len - ret);
                    temp->len = ret;
                } else {
                    /* Set count to 0 so that this handler will be
                       ignored on the next interation. */
                    temp->cnt = 0;

                    /* Reset the buffer pointer and length. */
                    temp->buf = NULL;
                    temp->len = 0;
                }

                /* If the remaining count is above zero, add it to the
                   accumulator. */
                if (temp->cnt > 0)
                    cnt_acc |= temp->cnt;
            }
        }

        temp = temp->next;

        if (temp == NULL)
            break;
    }

    /* Return 0 if all handlers have returned 0 or all the counts are otherwise 0. */
    if (cnt_acc == 0)
        return 0;
    else
        return 1;
}

void
midi_in_sysex(uint8_t *buffer, uint32_t len)
{
    midi_start_sysex(buffer, len);

    while (1) {
        /* This will return 0 if all theh handlers have either
           timed out or otherwise indicated it is time to stop. */
        if (midi_do_sysex())
            plat_delay_ms(5); /* msec */
        else
            break;
    }
}

void
midi_reset(void)
{
    if (midi_out && midi_out->m_out_device && midi_out->m_out_device->reset)
        midi_out->m_out_device->reset();
}
