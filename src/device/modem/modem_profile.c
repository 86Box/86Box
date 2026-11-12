/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Modem settings stored by AT&W, restored by ATZ and at startup.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/nvr.h>
#include <86box/version.h>
#include <86box/modem/modem_voice.h>
#include <86box/modem/modem_common.h>
#include <86box/modem/modem_profile.h>

static void
modem_profile_capture(modem_profile_state_t s, uint8_t *data)
{
    data[0] = (uint8_t) *s.dcdmode;
    data[1] = (uint8_t) *s.dtrmode;
    data[2] = (uint8_t) *s.flowcontrol;
    data[3] = (uint8_t) *s.speaker_mode;
    data[4] = (uint8_t) *s.speaker_level;
    data[5] = (uint8_t) *s.pulse_dial;
    data[6] = (uint8_t) *s.doresponse;
    data[7] = (uint8_t) *s.echo;
    data[8] = (uint8_t) *s.numericresponse;
    memcpy(data + 9, s.reg, MODEM_REGS);
    data[9 + MREG_RING_COUNT] = 0;
    data[9 + MODEM_REGS] = (uint8_t) *s.result_mode;
    data[10 + MODEM_REGS] = (uint8_t) *s.connect_report;
}

static bool
modem_profile_valid(const uint8_t *data)
{
    return data[0] <= 1 && data[1] <= 3 && data[2] <= 4 && data[3] <= 3
           && data[4] <= 3 && data[5] <= 1 && data[6] <= 2
           && data[7] <= 1 && data[8] <= 1
           && data[9 + MREG_RING_COUNT] == 0
           && data[9 + MODEM_REGS] <= 4 && data[10 + MODEM_REGS] <= 2;
}

static void
modem_profile_apply(modem_profile_state_t s, const uint8_t *data)
{
    uint8_t ring_count = s.reg[MREG_RING_COUNT];
    *s.dcdmode = data[0];
    *s.dtrmode = data[1];
    *s.flowcontrol = data[2];
    *s.speaker_mode = data[3];
    *s.speaker_level = data[4];
    *s.pulse_dial = data[5];
    *s.doresponse = data[6];
    *s.echo = data[7] != 0;
    *s.numericresponse = data[8] != 0;
    memcpy(s.reg, data + 9, MODEM_REGS);
    s.reg[MREG_RING_COUNT] = ring_count;
    *s.result_mode = data[9 + MODEM_REGS];
    *s.connect_report = data[10 + MODEM_REGS];
}

void
modem_profile_init(modem_profile_t *profile, modem_profile_state_t state,
                   const char *machine, const char *frontend, int instance)
{
    static const uint8_t magic[] = { '8', '6', 'M', 'D' };
    uint8_t header[sizeof(magic)];
    uint8_t data[sizeof(profile->saved)];
    FILE *fp;

    modem_profile_capture(state, profile->saved);
    memcpy(profile->factory, profile->saved, sizeof(profile->factory));
    if (snprintf(profile->filename, sizeof(profile->filename), "%s_%s_modem_%d.nvr",
                 machine, frontend, instance) >= (int) sizeof(profile->filename)) {
        profile->filename[0] = '\0';
        pclog("Modem: profile filename is too long\n");
        return;
    }

    errno = 0;
    fp = nvr_fopen(profile->filename, "rb");
    if (!fp) {
        if (errno != ENOENT)
            pclog("Modem: cannot open profile %s: %s\n", profile->filename, strerror(errno));
        return;
    }
    uint8_t version = 0;
    memcpy(data, profile->factory, sizeof(data));
    bool valid = fread(header, 1, sizeof(header), fp) == sizeof(header)
                 && !memcmp(header, magic, sizeof(header))
                 && fread(&version, 1, 1, fp) == 1
                 && (version == 1 || version == 2)
                 && fread(data, 1, version == 1 ? 9 + MODEM_REGS : sizeof(data), fp)
                        == (version == 1 ? 9 + MODEM_REGS : sizeof(data))
                 && fgetc(fp) == EOF && !ferror(fp);
    if (valid && version == 1) {
        const unsigned indices[] = { MREG_DIAL_WAIT, MREG_CARRIER_WAIT,
                                     MREG_CARRIER_LOSS, MREG_DTMF_DURATION };
        for (size_t i = 0; i < sizeof(indices) / sizeof(indices[0]); i++) {
            if (!data[9 + indices[i]])
                data[9 + indices[i]] = profile->factory[9 + indices[i]];
        }
    }
    valid = valid && modem_profile_valid(data);
    if (fclose(fp) != 0)
        valid = false;
    if (!valid) {
        pclog("Modem: invalid or unreadable profile %s\n", profile->filename);
        return;
    }
    modem_profile_apply(state, data);
    memcpy(profile->saved, data, sizeof(data));
}

static bool
modem_profile_write_impl(modem_profile_t *profile, modem_profile_state_t state, bool force)
{
    static const uint8_t magic[] = { '8', '6', 'M', 'D', 2 };
    uint8_t data[sizeof(profile->saved)];
    FILE *fp;
    bool written;

    modem_profile_capture(state, data);
    if (!profile->filename[0]) {
        pclog("Modem: cannot save profile without a valid filename\n");
        return false;
    }
    if (!force && !memcmp(data, profile->saved, sizeof(data)))
        return true;
    fp = nvr_fopen(profile->filename, "wb");
    if (!fp) {
        pclog("Modem: cannot save profile %s: %s\n", profile->filename, strerror(errno));
        return false;
    }
    written = fwrite(magic, 1, sizeof(magic), fp) == sizeof(magic)
              && fwrite(data, 1, sizeof(data), fp) == sizeof(data);
    if (fclose(fp) != 0)
        written = false;
    if (!written) {
        pclog("Modem: failed writing profile %s\n", profile->filename);
        return false;
    }
    memcpy(profile->saved, data, sizeof(data));
    return true;
}

void
modem_profile_save(modem_profile_t *profile, modem_profile_state_t state)
{
    modem_profile_write_impl(profile, state, false);
}

bool
modem_profile_write(modem_profile_t *profile, modem_profile_state_t state)
{
    return modem_profile_write_impl(profile, state, true);
}

void
modem_profile_restore(const modem_profile_t *profile, modem_profile_state_t state)
{
    modem_profile_apply(state, profile->saved);
}

void
modem_profile_factory(const modem_profile_t *profile, modem_profile_state_t state)
{
    modem_profile_apply(state, profile->factory);
}

const char *
modem_ati_identification(unsigned index)
{
    switch (index) {
        case 0: return "86Box Emulated Modem";
        case 3: return "86Box Emulated Modem Firmware V1.00";
        case 4: return "Modem compiled for 86Box version " EMU_VERSION;
        default: return NULL;
    }
}

ResTypes
modem_result_for_mode(ResTypes response, int mode)
{
    if ((response == ResNODIALTONE && mode != 2 && mode != 4)
        || (response == ResBUSY && mode != 3 && mode != 4)
        || (response == ResNOANSWER && mode != 4))
        return ResNOCARRIER;
    return response;
}

uint32_t
modem_nominal_baudrate(uint32_t baud)
{
    static const uint32_t rates[] = {
        75, 110, 150, 300, 600, 1200, 1800, 2400, 4800, 7200, 9600, 12000,
        14400, 16800, 19200, 21600, 24000, 26400, 28800, 31200, 33600,
        38400, 56000, 57600, 115200, 230400
    };
    uint32_t nominal = baud;
    uint32_t best_delta = UINT32_MAX;

    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        const uint32_t rate = rates[i];
        const uint32_t delta = baud > rate ? baud - rate : rate - baud;
        if (delta <= rate / 50 && delta < best_delta) {
            nominal = rate;
            best_delta = delta;
        }
    }
    return nominal;
}

unsigned
modem_connect_result_code(uint32_t baud)
{
    switch (modem_nominal_baudrate(baud)) {
        case 1200: return 5;
        case 2400: return 10;
        case 4800: return 11;
        case 9600: return 12;
        case 7200: return 13;
        case 14400: return 15;
        case 19200: return 16;
        case 38400: return 17;
        case 57600: return 18;
        case 115200: return 19;
        default: return 1;
    }
}

const char *
modem_identification(const char *command)
{
    if (!strcmp(command, "GMI"))
        return "86Box";
    if (!strcmp(command, "GMM"))
        return "86Box Emulated Modem";
    if (!strcmp(command, "GMR"))
        return "86Box " EMU_VERSION " firmware";
    return NULL;
}

int
modem_fclass_command(const char *command, uint8_t support, uint8_t *fax_class,
                     uint8_t *voice_class, bool *voice_v253, int *voice_media_type,
                     int *voice_rate, char *response, size_t size)
{
    response[0] = '\0';
    if (!strcmp(command, "FCLASS=?")) {
        if (!support)
            return -1;
        snprintf(response, size, "0%s%s", (support & MODEM_FAX_SUPPORT_CLASS_1) ? ",1.0" : "",
                 (support & MODEM_FAX_SUPPORT_CLASS_8) ? ",8" : "");
    } else if (!strcmp(command, "FCLASS?")) {
        const char *current = *fax_class == 0 ? "0" : *fax_class == 1 ? "1.0"
                                                       : *fax_class == 8 ? "8" : NULL;
        if (!support || !current)
            return -1;
        snprintf(response, size, "%s", current);
    } else if (!strcmp(command, "FCLASS=0") || !strcmp(command, "FCLASS=0.0")) {
        if (!support)
            return -1;
        *fax_class = *voice_class = 0;
    } else if (!strcmp(command, "FCLASS=1") || !strcmp(command, "FCLASS=1.0")) {
        if (!(support & MODEM_FAX_SUPPORT_CLASS_1))
            return -1;
        *fax_class = 1;
        *voice_class = 0;
    } else if (!strcmp(command, "FCLASS=8") || !strcmp(command, "FCLASS=8.0")) {
        if (!(support & MODEM_FAX_SUPPORT_CLASS_8))
            return -1;
        *fax_class = *voice_class = 8;
        *voice_v253 = true;
        *voice_media_type = VOICE_FRAME_AUDIO;
        *voice_rate = 8000;
    } else {
        return strncmp(command, "FCLASS=", 7) ? 0 : -1;
    }
    return 1;
}
