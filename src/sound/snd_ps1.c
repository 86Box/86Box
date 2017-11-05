#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../io.h"
#include "../pic.h"
#include "../timer.h"
#include "../device.h"
#include "sound.h"
#include "snd_ps1.h"
#include "snd_sn76489.h"


typedef struct ps1_audio_t
{
        sn76489_t sn76489;
        
        uint8_t status, ctrl;
        
        int64_t timer_latch, timer_count, timer_enable;
        
        uint8_t fifo[2048];
        int fifo_read_idx, fifo_write_idx;
        int fifo_threshold;
        
        uint8_t dac_val;

        int16_t buffer[SOUNDBUFLEN];
        int pos;
} ps1_audio_t;

static void ps1_update_irq_status(ps1_audio_t *ps1)
{
        if (((ps1->status & ps1->ctrl) & 0x12) && (ps1->ctrl & 0x01))
                picint(1 << 7);
        else
                picintc(1 << 7);
}

static uint8_t ps1_audio_read(uint16_t port, void *p)
{
        ps1_audio_t *ps1 = (ps1_audio_t *)p;
        uint8_t temp;

        switch (port & 7)
        {
                case 0: /*ADC data*/
                ps1->status &= ~0x10;
                ps1_update_irq_status(ps1);
                return 0;
                case 2: /*Status*/
                temp = ps1->status;
                temp |= (ps1->ctrl & 0x01);
                if ((ps1->fifo_write_idx - ps1->fifo_read_idx) >= 2048)
                        temp |= 0x08; /*FIFO full*/
                if (ps1->fifo_read_idx == ps1->fifo_write_idx)
                        temp |= 0x04; /*FIFO empty*/
                return temp;
                case 3: /*FIFO timer*/
                /*PS/1 technical reference says this should return the current value,
                  but the PS/1 BIOS and Stunt Island expect it not to change*/
                return ps1->timer_latch;
                case 4: case 5: case 6: case 7:
                return 0;
        }
        return 0xff;
}

static void ps1_audio_write(uint16_t port, uint8_t val, void *p)
{
        ps1_audio_t *ps1 = (ps1_audio_t *)p;

        switch (port & 7)
        {
                case 0: /*DAC output*/
                if ((ps1->fifo_write_idx - ps1->fifo_read_idx) < 2048)
                {
                        ps1->fifo[ps1->fifo_write_idx & 2047] = val;
                        ps1->fifo_write_idx++;
                }
                break;
                case 2: /*Control*/
                ps1->ctrl = val;
                if (!(val & 0x02))
                        ps1->status &= ~0x02;
                ps1_update_irq_status(ps1);
                break;
                case 3: /*Timer reload value*/
                ps1->timer_latch = val;
                ps1->timer_count = (int64_t) ((0xff-val) * TIMER_USEC);
                ps1->timer_enable = (val != 0);
                break;
                case 4: /*Almost empty*/
                ps1->fifo_threshold = val * 4;
                break;
        }       
}

static void ps1_audio_update(ps1_audio_t *ps1)
{
        for (; ps1->pos < sound_pos_global; ps1->pos++)        
                ps1->buffer[ps1->pos] = (int8_t)(ps1->dac_val ^ 0x80) * 0x20;
}

static void ps1_audio_callback(void *p)
{
        ps1_audio_t *ps1 = (ps1_audio_t *)p;
        
        ps1_audio_update(ps1);
        
        if (ps1->fifo_read_idx != ps1->fifo_write_idx)
        {
                ps1->dac_val = ps1->fifo[ps1->fifo_read_idx & 2047];
                ps1->fifo_read_idx++;
        }
        if ((ps1->fifo_write_idx - ps1->fifo_read_idx) == ps1->fifo_threshold)
        {
                ps1->status |= 0x02; /*FIFO almost empty*/
        }
        ps1->status |= 0x10; /*ADC data ready*/
        ps1_update_irq_status(ps1);
        
        ps1->timer_count += ps1->timer_latch * TIMER_USEC;
}

static void ps1_audio_get_buffer(int32_t *buffer, int len, void *p)
{
        ps1_audio_t *ps1 = (ps1_audio_t *)p;
        int c;
        
        ps1_audio_update(ps1);
        
        for (c = 0; c < len * 2; c++)
                buffer[c] += ps1->buffer[c >> 1];

        ps1->pos = 0;
}

static void *ps1_audio_init(device_t *info)
{
        ps1_audio_t *ps1 = malloc(sizeof(ps1_audio_t));
        memset(ps1, 0, sizeof(ps1_audio_t));

        sn76489_init(&ps1->sn76489, 0x0205, 0x0001, SN76496, 4000000);

        io_sethandler(0x0200, 0x0001, ps1_audio_read, NULL, NULL, ps1_audio_write, NULL, NULL, ps1);
        io_sethandler(0x0202, 0x0006, ps1_audio_read, NULL, NULL, ps1_audio_write, NULL, NULL, ps1);
        timer_add(ps1_audio_callback, &ps1->timer_count, &ps1->timer_enable, ps1);
        sound_add_handler(ps1_audio_get_buffer, ps1);
        
        return ps1;
}

static void ps1_audio_close(void *p)
{
        ps1_audio_t *ps1 = (ps1_audio_t *)p;

        free(ps1);
}

device_t ps1_audio_device =
{
        "PS/1 Audio Card",
        0, 0,
        ps1_audio_init,
        ps1_audio_close,
	NULL,
        NULL,
        NULL,
        NULL,
        NULL
};
