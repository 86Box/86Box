/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Shared stubs for modem reset tests.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
static int test_speaker_mode;
static int test_speaker_level;

void pclog(UNUSED(const char *format), ...) {}
void sound_in_stop_input(void) {}
void modem_sound_event(UNUSED(modem_sound_t *sound), UNUSED(int type),
                       UNUSED(const char *number), UNUSED(int arg)) {}
void modem_sound_speaker(UNUSED(modem_sound_t *sound), int mode, int level)
{
    test_speaker_mode = mode;
    test_speaker_level = level;
}
void ppp_close(UNUSED(ppp_ctx_t *ctx)) {}
void slip_auth_close(UNUSED(slip_auth_ctx_t *ctx)) {}
void ppp_rx_byte(UNUSED(ppp_ctx_t *ctx), UNUSED(uint8_t byte)) { abort(); }
bool slip_auth_rx_byte(UNUSED(slip_auth_ctx_t *ctx), UNUSED(uint8_t byte)) { abort(); }
void modem_ras_rx_byte(UNUSED(modem_ras_decoder_t *decoder), UNUSED(uint8_t byte)) { abort(); }
void fatal(UNUSED(const char *format), ...) { abort(); }
void modem_ras_decoder_init(UNUSED(modem_ras_decoder_t *decoder),
                            UNUSED(modem_ras_frame_callback_t callback),
                            UNUSED(modem_ras_event_callback_t event_callback),
                            UNUSED(void *priv)) {}
void voice_silence_init(UNUSED(voice_silence_t *state), UNUSED(int sensitivity)) {}
void plat_netsocket_close(UNUSED(SOCKET socket)) {}
SOCKET plat_netsocket_accept(UNUSED(SOCKET socket)) { return (SOCKET) -1; }
SOCKET plat_netsocket_create_server(UNUSED(int type), UNUSED(unsigned short port))
{
    return (SOCKET) -1;
}
