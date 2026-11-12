/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Stubs for the sound, voice, PPP and SLIP functions that the modem
 *          command parser references. Tests that drive AT commands reach them,
 *          but they never call them, so each stub aborts if it is ever reached.
 */
#ifndef MODEM_COMMAND_TEST_STUBS_H
#define MODEM_COMMAND_TEST_STUBS_H

int
al_capture_get_rate(void)
{
    abort();
}

int
cslip_decompress_packet(UNUSED(cslip_ctx_t *ctx), UNUSED(const uint8_t *in), UNUSED(int in_len),
                        UNUSED(uint8_t *out))
{
    abort();
}

uint32_t
modem_sound_connect_for(UNUSED(modem_sound_t *sound), UNUSED(uint32_t baud))
{
    abort();
}

uint32_t
modem_sound_dial_ms(UNUSED(const char *number), UNUSED(int s6), UNUSED(int s8), UNUSED(int s11),
                    UNUSED(int pulse), UNUSED(int detect))
{
    abort();
}

int
modem_sound_full(UNUSED(modem_sound_t *sound))
{
    abort();
}

uint32_t
modem_sound_handshake_for(UNUSED(modem_sound_t *sound), UNUSED(uint32_t baud))
{
    abort();
}

uint32_t
modem_sound_ring_ms(UNUSED(modem_sound_t *sound))
{
    abort();
}

void
network_tx(UNUSED(netcard_t *card), UNUSED(uint8_t *data), UNUSED(int len))
{
    abort();
}

SOCKET
plat_netsocket_create(UNUSED(int type))
{
    abort();
}

int
plat_netsocket_connect(UNUSED(SOCKET socket), UNUSED(const char *hostname), UNUSED(unsigned short port))
{
    abort();
}

ppp_ctx_t *
ppp_init(UNUSED(void *modem), UNUSED(void *log),
         UNUSED(void (*serial_push)(void *, const uint8_t *, int)),
         UNUSED(void (*network_send_ip)(void *, const uint8_t *, int)))
{
    abort();
}

void
ppp_mppe_set_policy(UNUSED(ppp_ctx_t *ctx), UNUSED(uint32_t bitmask), UNUSED(uint8_t format))
{
    abort();
}

void
ppp_multilink_configure(UNUSED(ppp_ctx_t *ctx), UNUSED(const char *group))
{
    abort();
}

void
ppp_start(UNUSED(ppp_ctx_t *ctx))
{
    abort();
}

size_t
rv_adpcm_decode(UNUSED(rv_adpcm_t *s), UNUSED(const uint8_t *in), UNUSED(size_t n),
                UNUSED(int16_t *out), UNUSED(size_t max))
{
    abort();
}

size_t
rv_adpcm_flush(UNUSED(rv_adpcm_t *s), UNUSED(uint8_t *out), UNUSED(size_t max))
{
    abort();
}

void
rv_adpcm_init(UNUSED(rv_adpcm_t *s), UNUSED(int bps))
{
    abort();
}

slip_auth_ctx_t *
slip_auth_init(UNUSED(void *modem), UNUSED(void *log),
               UNUSED(void (*serial_push)(void *, const uint8_t *, int)),
               UNUSED(const char *username), UNUSED(const char *password))
{
    abort();
}

void
slip_auth_start(UNUSED(slip_auth_ctx_t *ctx))
{
    abort();
}

bool
slip_decode_frame_logged(UNUSED(void *log), UNUSED(const uint8_t *frame), UNUSED(size_t frame_len),
                         UNUSED(uint8_t *packet), UNUSED(size_t packet_capacity),
                         UNUSED(size_t *packet_len))
{
    abort();
}

bool
slip_packet_looks_like_ppp(UNUSED(const uint8_t *packet), UNUSED(size_t packet_len))
{
    abort();
}

void
sound_in_start_input(void)
{
    abort();
}

int16_t
voice_alaw_decode(UNUSED(uint8_t a))
{
    abort();
}

uint8_t
voice_alaw_encode(UNUSED(int16_t x))
{
    abort();
}

int
voice_dtmf_freqs(UNUSED(char digit), UNUSED(double *f1), UNUSED(double *f2))
{
    abort();
}

size_t
voice_frame(UNUSED(uint8_t *out), UNUSED(int type), UNUSED(const uint8_t *payload), UNUSED(size_t len))
{
    abort();
}

size_t
voice_resample(UNUSED(voice_resampler_t *r), UNUSED(const int16_t *in), UNUSED(size_t n),
               UNUSED(int16_t *out), UNUSED(size_t max))
{
    abort();
}

void
voice_resampler_init(UNUSED(voice_resampler_t *r), UNUSED(uint32_t from), UNUSED(uint32_t to))
{
    abort();
}

int
voice_tone_mix(UNUSED(voice_tone_t *t), UNUSED(int16_t *buf), UNUSED(size_t n))
{
    abort();
}

void
voice_tone_start(UNUSED(voice_tone_t *t), UNUSED(double f1), UNUSED(double f2), UNUSED(uint32_t ms),
                 UNUSED(double amp))
{
    abort();
}

int16_t
voice_ulaw_decode(UNUSED(uint8_t u))
{
    abort();
}

uint8_t
voice_ulaw_encode(UNUSED(int16_t x))
{
    abort();
}

#endif /*MODEM_COMMAND_TEST_STUBS_H*/
