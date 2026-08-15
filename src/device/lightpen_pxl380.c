#include <stdint.h>

#include <86box/86box.h>
#include <86box/video.h>
#include <86box/mouse.h>
#include <86box/pic.h>


#define TIMER_MASK 0xfff
struct pxl380_t
{
    uint16_t timer0_cnt;

    uint16_t timer0_cnt_latch;
    uint16_t timer1_cnt_latch;

    int irq_num;
    int irq_enabled;

    uint8_t status;
};

typedef struct pxl380_t pxl380_t;

static float
pxl380_sample_luma(bitmap_t* target_buffer, uint32_t x, uint32_t y)
{
    const float R_COEFF    = 0.3;
    const float G_COEFF    = 0.4;
    const float B_COEFF    = 0.7;
    float total_luma       = 0.0;

    // Can't sample unrendered lines...
    for (int ky = -2; ky <= 0; ky++) {
        for (int kx = -2; kx <= 2; kx++) {
            uint32_t xx = (((int)x) + kx) & 2047;
            uint32_t yy = (((int)y) + ky) & 2047;

            float r = ((target_buffer->line[yy][xx] >> 16) & 0xFF) / 255.0;
            float g = ((target_buffer->line[yy][xx] >> 8) & 0xFF) / 255.0;
            float b = (target_buffer->line[yy][xx] & 0xFF) / 255.0;

            total_luma += (r * R_COEFF) + (g * G_COEFF) + (b * B_COEFF);
        }
    }

    return total_luma / 15.0;
}

void pxl380_lightpen_check_trigger_strobe(void* priv, int x_offset, int y, int x_offset_from_hsync, int firstline, double hpix_clock, int monitor_used)
{
    pxl380_t* pxl380 = priv;
    double abs_x, abs_y;

    mouse_get_abs_coords(&abs_x, &abs_y);

    abs_x *= monitors[monitor_used].mon_unscaled_size_x - 1;
    abs_y *= monitors[monitor_used].mon_efscrnsz_y - 1;

    int x = abs_x + x_offset;
    float sampled_luma = pxl380_sample_luma(monitors[monitor_used].target_buffer, x, y);
    x -= x_offset;
    y -= firstline;
    
    if (enable_overscan) {
        x += x_offset;
        y += monitors[monitor_used].mon_overscan_y / 2;
        x_offset = 0;
    }

    // Checking for x makes no sense.
    if (sampled_luma >= 0.125 && y == abs_y) {
        double factor = 48000000. / hpix_clock;
        int latch_val = (x_offset_from_hsync + x + x_offset) * factor;

        pxl380->timer1_cnt_latch  = latch_val & 0xfff;
        pxl380->timer0_cnt_latch  = pxl380->timer0_cnt & 0xfff;
        pxl380->status           |= 0x8;
        if (pxl380->irq_enabled)
            picint(1 << pxl380->irq_num);
    }
}
