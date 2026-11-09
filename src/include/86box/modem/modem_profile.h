/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Persistent guest-programmed modem settings.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#ifndef EMU_MODEM_PROFILE_H
#define EMU_MODEM_PROFILE_H

typedef struct modem_profile_state_t {
    int *dcdmode, *dtrmode, *flowcontrol, *speaker_mode, *speaker_level;
    int *pulse_dial, *doresponse;
    bool *echo, *numericresponse;
    uint8_t *reg;
    int *result_mode, *connect_report;
} modem_profile_state_t;

typedef struct modem_profile_t {
    char filename[512];
    uint8_t saved[11 + MODEM_REGS];
    uint8_t factory[11 + MODEM_REGS];
} modem_profile_t;

void modem_profile_init(modem_profile_t *profile, modem_profile_state_t state,
                        const char *machine, const char *frontend, int instance);
void modem_profile_save(modem_profile_t *profile, modem_profile_state_t state);
bool modem_profile_write(modem_profile_t *profile, modem_profile_state_t state);
void modem_profile_restore(const modem_profile_t *profile, modem_profile_state_t state);
void modem_profile_factory(const modem_profile_t *profile, modem_profile_state_t state);
const char *modem_identification(const char *command);
const char *modem_ati_identification(unsigned index);
ResTypes modem_result_for_mode(ResTypes response, int mode);
uint32_t modem_nominal_baudrate(uint32_t baud);
unsigned modem_connect_result_code(uint32_t baud);
int modem_fclass_command(const char *command, uint8_t support, uint8_t *fax_class,
                         uint8_t *voice_class, bool *voice_v253, int *voice_media_type,
                         int *voice_rate, char *response, size_t size);

#endif
