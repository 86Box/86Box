#include "ibm.h"
#include "sound_ym7128.h"

static int attenuation[32];
static int tap_position[32];

void ym7128_init(ym7128_t *ym7128)
{
        int c;
        double out = 65536.0;
        
        for (c = 0; c < 32; c++)
                tap_position[c] = c * (2400 / 31);

        for (c = 31; c >= 1; c--)
        {
                attenuation[c] = (int)out;
                out /= 1.25963; /*2 dB steps*/
        }
        attenuation[0] = 0;
}

#define GET_ATTENUATION(val) (val & 0x20) ? -attenuation[val & 0x1f] : attenuation[val & 0x1f]

void ym7128_write(ym7128_t *ym7128, uint8_t val)
{
        int new_dat = val & 1;
        int new_sci = val & 2;
        int new_a0 = val & 4;
//        pclog("ym7128_write %i %i %i\n", new_dat, new_sci, new_a0);
        if (!ym7128->sci && new_sci)
                ym7128->dat = (ym7128->dat << 1) | new_dat;
        
        if (ym7128->a0 != new_a0)
        {
//                pclog("ym7128 write %i %02x\n", ym7128->a0, ym7128->dat);
                if (!ym7128->a0)
                        ym7128->reg_sel = ym7128->dat & 0x1f;
                else
                {
//                        pclog("YM7128 write %02x %02x\n", ym7128->reg_sel, ym7128->dat);
                        switch (ym7128->reg_sel)
                        {
                                case 0x00: case 0x01: case 0x02: case 0x03:
                                case 0x04: case 0x05: case 0x06: case 0x07:
                                ym7128->gl[ym7128->reg_sel & 7] = GET_ATTENUATION(ym7128->dat);
//                                pclog(" GL[%i] = %04x\n", ym7128->reg_sel & 7, GET_ATTENUATION(ym7128->dat));
                                break;
                                case 0x08: case 0x09: case 0x0a: case 0x0b:
                                case 0x0c: case 0x0d: case 0x0e: case 0x0f:
                                ym7128->gr[ym7128->reg_sel & 7] = GET_ATTENUATION(ym7128->dat);
//                                pclog(" GR[%i] = %04x\n", ym7128->reg_sel & 7, GET_ATTENUATION(ym7128->dat));
                                break;
                                
                                case 0x10:
                                ym7128->vm = GET_ATTENUATION(ym7128->dat);
//                                pclog(" VM = %04x\n", GET_ATTENUATION(ym7128->dat));
                                break;
                                case 0x11:
                                ym7128->vc = GET_ATTENUATION(ym7128->dat);
//                                pclog(" VC = %04x\n", GET_ATTENUATION(ym7128->dat));
                                break;
                                case 0x12:
                                ym7128->vl = GET_ATTENUATION(ym7128->dat);
//                                pclog(" VL = %04x\n", GET_ATTENUATION(ym7128->dat));
                                break;
                                case 0x13:
                                ym7128->vr = GET_ATTENUATION(ym7128->dat);
//                                pclog(" VR = %04x\n", GET_ATTENUATION(ym7128->dat));
                                break;

                                case 0x14:
                                ym7128->c0 = (ym7128->dat & 0x3f) << 6;
                                if (ym7128->dat & 0x20)
                                        ym7128->c0 |= 0xfffff000;
                                break;
                                case 0x15:
                                ym7128->c1 = (ym7128->dat & 0x3f) << 6;
                                if (ym7128->dat & 0x20)
                                        ym7128->c1 |= 0xfffff000;
                                break;

                                case 0x16: case 0x17: case 0x18: case 0x19:
                                case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e:
                                ym7128->t[ym7128->reg_sel - 0x16] = tap_position[ym7128->dat & 0x1f];
//                                pclog(" T[%i] = %i\n", ym7128->reg_sel - 0x16, tap_position[ym7128->dat & 0x1f]);
                                break;
                        }
                        ym7128->regs[ym7128->reg_sel] = ym7128->dat;
                }
                ym7128->dat = 0;
        }

        ym7128->sci = new_sci;
        ym7128->a0 = new_a0;
}

#define GET_DELAY_SAMPLE(ym7128, offset) (((ym7128->delay_pos - offset) < 0) ? ym7128->delay_buffer[(ym7128->delay_pos - offset) + 2400] : ym7128->delay_buffer[ym7128->delay_pos - offset])

void ym7128_apply(ym7128_t *ym7128, int16_t *buffer, int len)
{
        int c, d;
        
        for (c = 0; c < len*2; c += 4)
        {
                /*YM7128 samples a mono stream at ~24 kHz, so downsample*/
                int32_t samp = ((int32_t)buffer[c] + (int32_t)buffer[c+1] + (int32_t)buffer[c+2] + (int32_t)buffer[c+3]) / 4;
                int32_t filter_temp, filter_out;
                int32_t samp_l = 0, samp_r = 0;

                filter_temp = GET_DELAY_SAMPLE(ym7128, ym7128->t[0]);
                filter_out = ((filter_temp * ym7128->c0) >> 11) + ((ym7128->filter_dat * ym7128->c1) >> 11);
                filter_out = (filter_out * ym7128->vc) >> 16;

                samp = (samp * ym7128->vm) >> 16;
                samp += filter_out;
                
                ym7128->delay_buffer[ym7128->delay_pos] = samp;
                
                for (d = 0; d < 8; d++)
                {
                        samp_l += (GET_DELAY_SAMPLE(ym7128, ym7128->t[d+1]) * ym7128->gl[d]) >> 16;
                        samp_r += (GET_DELAY_SAMPLE(ym7128, ym7128->t[d+1]) * ym7128->gr[d]) >> 16;
                }
                
                samp_l = (samp_l * ym7128->vl*2) >> 16;
                samp_r = (samp_r * ym7128->vr*2) >> 16;
                
                buffer[c]   += ((int32_t)samp_l + (int32_t)ym7128->prev_l) / 2;
                buffer[c+1] += ((int32_t)samp_r + (int32_t)ym7128->prev_r) / 2;
                buffer[c+2] += samp_l;
                buffer[c+3] += samp_r;
                
                ym7128->delay_pos++;
                if (ym7128->delay_pos >= 2400)
                        ym7128->delay_pos = 0;
                        
                ym7128->filter_dat = filter_temp;
                ym7128->prev_l = samp_l;
                ym7128->prev_r = samp_r;
        }
}
