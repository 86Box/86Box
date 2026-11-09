/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Shared modem configuration tests.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
static int configured_fax_support;
static int configured_handshake;
static int configured_sound_standard;
static int applied_handshake;
static int applied_sound_standard;
static const char *configured_multilink_group;

static const struct {
    int value;
    const char *id;
} test_pnp_profiles[] = {
    { MODEM_PNP_PNPC100, "PNPC100" },
    { MODEM_PNP_PNPC101, "PNPC101" },
    { MODEM_PNP_PNPC102, "PNPC102" },
    { MODEM_PNP_GENERIC, "PNPC103" },
    { MODEM_PNP_PNPC104, "PNPC104" },
    { MODEM_PNP_PNPC105, "PNPC105" },
    { MODEM_PNP_PNPC106, "PNPC106" },
    { MODEM_PNP_ENHANCED, "PNPC107" }
};

int
device_get_config_int(const char *name)
{
    if (!strcmp(name, "fax_voice_support"))
        return configured_fax_support;
    if (!strcmp(name, "sound_handshake"))
        return configured_handshake;
    if (!strcmp(name, "sound_standard"))
        return configured_sound_standard;
    return 0;
}

void
modem_sound_set_handshake(UNUSED(modem_sound_t *sound), int handshake, int standard)
{
    applied_handshake = handshake;
    applied_sound_standard = standard;
}

const char *
device_get_config_string(const char *name)
{
    return !strcmp(name, "ppp_multilink_group") ? configured_multilink_group : "";
}

static int
test_modem_config(void)
{
    const device_config_t *fax = NULL;
    const device_config_t *group = NULL;
    const device_config_t *pnp = NULL;
    const device_config_t *handshake = NULL;
    const device_config_t *standard = NULL;
    modem_t modem = { 0 };

    for (const device_config_t *entry = modem_config; entry->type != CONFIG_END; entry++) {
        if (!strcmp(entry->name, "fax_voice_support"))
            fax = entry;
        else if (!strcmp(entry->name, "ppp_multilink_group"))
            group = entry;
        else if (!strcmp(entry->name, "serial_pnp"))
            pnp = entry;
        else if (!strcmp(entry->name, "sound_handshake"))
            handshake = entry;
        else if (!strcmp(entry->name, "sound_standard"))
            standard = entry;
    }

    if (!pnp || pnp->type != CONFIG_SELECTION || pnp->default_int != MODEM_PNP_DISABLED
        || MODEM_PNP_DISABLED != 0 || MODEM_PNP_GENERIC != 1 || MODEM_PNP_ENHANCED != 2) {
        fprintf(stderr, "Serial PnP configuration or legacy profile values are incorrect\n");
        return 1;
    }
    size_t pnp_count = 0;
    size_t disabled_count = 0;
    for (size_t i = 0; i < sizeof(pnp->selection) / sizeof(pnp->selection[0]); i++) {
        const device_config_selection_t *selection = &pnp->selection[i];
        if (!selection->description || !selection->description[0])
            break;
        pnp_count++;
        if (selection->value == MODEM_PNP_DISABLED)
            disabled_count++;
    }
    if (pnp_count != sizeof(test_pnp_profiles) / sizeof(test_pnp_profiles[0]) + 1
        || disabled_count != 1) {
        fprintf(stderr, "Serial PnP selection count is incorrect\n");
        return 1;
    }
    for (size_t i = 0; i < sizeof(test_pnp_profiles) / sizeof(test_pnp_profiles[0]); i++) {
        size_t matches = 0;
        for (size_t j = 0; j < pnp_count; j++) {
            if (pnp->selection[j].value == test_pnp_profiles[i].value
                && strstr(pnp->selection[j].description, test_pnp_profiles[i].id))
                matches++;
        }
        if (matches != 1) {
            fprintf(stderr, "Serial PnP selection %s is missing or duplicated\n", test_pnp_profiles[i].id);
            return 1;
        }
    }

    if (!group || group->type != CONFIG_STRING || !group->default_string
        || group->default_string[0] != '\0') {
        fprintf(stderr, "PPP multilink group is missing or has the wrong default\n");
        return 1;
    }
#ifdef ENABLE_MODEM_ADVANCED_CONFIG
    if (!handshake || handshake->type != CONFIG_SELECTION
        || handshake->default_int != MODEM_SOUND_HANDSHAKE_FULL) {
        fprintf(stderr, "Advanced handshake configuration is missing or has the wrong default\n");
        return 1;
    }
    if (!fax || fax->type != CONFIG_SELECTION || fax->default_int != MODEM_FAX_SUPPORT_DEFAULT) {
        fprintf(stderr, "Advanced fax configuration is missing or has the wrong default\n");
        return 1;
    }
    bool found_default = false;
    for (size_t i = 0; i < sizeof(fax->selection) / sizeof(fax->selection[0]); i++) {
        if (fax->selection[i].description && fax->selection[i].description[0]
            && fax->selection[i].value == MODEM_FAX_SUPPORT_DEFAULT)
            found_default = true;
    }
    if (!found_default) {
        fprintf(stderr, "Default fax capabilities are not selectable\n");
        return 1;
    }
#else
    if (handshake) {
        fprintf(stderr, "Handshake configuration should be hidden in normal builds\n");
        return 1;
    }
    if (fax) {
        fprintf(stderr, "Fax configuration should be hidden in normal builds\n");
        return 1;
    }
#endif
    if (!standard || standard->type != CONFIG_SELECTION
        || standard->default_int != MODEM_SOUND_STD_AUTO) {
        fprintf(stderr, "Sound standard configuration is missing or has the wrong default\n");
        return 1;
    }
    configured_sound_standard = MODEM_SOUND_STD_V34;
    for (int setting = MODEM_SOUND_HANDSHAKE_SIMPLE; setting <= MODEM_SOUND_HANDSHAKE_OFF; setting++) {
        configured_handshake = setting;
        applied_handshake = -1;
        applied_sound_standard = -1;
        modem_load_sound_config(NULL);
#ifdef ENABLE_MODEM_ADVANCED_CONFIG
        if (applied_handshake != setting)
#else
        if (applied_handshake != MODEM_SOUND_HANDSHAKE_FULL)
#endif
        {
            fprintf(stderr, "Runtime handshake mode does not match the build configuration\n");
            return 1;
        }
        if (applied_sound_standard != configured_sound_standard) {
            fprintf(stderr, "Runtime handshake standard was not loaded\n");
            return 1;
        }
    }
    configured_fax_support = MODEM_FAX_SUPPORT_CLASS_8;
    configured_multilink_group = "shared-bundle";
    modem_load_class_and_multilink_config(&modem);
#ifdef ENABLE_MODEM_ADVANCED_CONFIG
    if (modem.fax_support != MODEM_FAX_SUPPORT_CLASS_8)
#else
    if (modem.fax_support != MODEM_FAX_SUPPORT_DEFAULT)
#endif
    {
        fprintf(stderr, "Runtime fax capabilities do not match the build configuration\n");
        return 1;
    }
    if (strcmp(modem.ppp_multilink_group, configured_multilink_group)) {
        fprintf(stderr, "Runtime multilink group was not loaded\n");
        return 1;
    }
    configured_multilink_group = "";
    modem_load_class_and_multilink_config(&modem);
    if (modem.ppp_multilink_group[0]) {
        fprintf(stderr, "Empty multilink group was not loaded\n");
        return 1;
    }
    return 0;
}
