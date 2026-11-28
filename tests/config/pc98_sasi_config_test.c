/* Exercise the production disk configuration and INI serializer across
 * cold reloads. Host file/path services and optional drive audio are adapted. */
#include "../../src/config.c"

char            usr_path[1024] = "C:/config-test/vm/";
char            exe_path[2048] = "C:/config-test/bin/";
static unsigned checks, failures;
#define CHECK(e)                                                    \
    do {                                                            \
        checks++;                                                   \
        if (!(e)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); \
            failures++;                                             \
        }                                                           \
    } while (0)

void
fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

#ifdef _WIN32
size_t
mbstoc16s(uint16_t dst[], const char src[], int len)
{
    return MultiByteToWideChar(CP_UTF8, 0, src, -1, (LPWSTR) dst, len);
}
size_t
c16stombs(char dst[], const uint16_t src[], int len)
{
    return WideCharToMultiByte(CP_UTF8, 0, (LPCWCH) src, -1, dst, len, NULL, NULL);
}
#else
#    include <strings.h>
int
strnicmp(const char *a, const char *b, size_t len)
{
    return strncasecmp(a, b, len);
}
#endif

FILE *
plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}
FILE *
rom_fopen(const char *path, char *mode)
{
    return fopen(path, mode);
}
void
hdd_audio_load_profiles(void)
{
}
int
hdd_audio_get_profile_by_internal_name(const char *name)
{
    return 0;
}
const char *
hdd_audio_get_profile_internal_name(int id)
{
    return "none";
}
int
path_abs(char *path)
{
    return path[0] == '/' || path[1] == ':';
}
void
path_normalize(char *path)
{
    for (; *path; ++path)
        if (*path == '\\')
            *path = '/';
}
const char *
path_get_slash(char *path)
{
    return path[strlen(path) - 1] == '/' ? "" : "/";
}

static void
set_disk(int slot, int bus, int unit)
{
    hdd[slot].bus_type = bus;
    hdd[slot].channel  = unit;
    hdd[slot].spt      = 17;
    hdd[slot].hpc      = 4;
    hdd[slot].tracks   = 615;
    snprintf(hdd[slot].fn, sizeof(hdd[slot].fn), "%sdisk%d.hdi", usr_path, slot);
}

static void
reload(const char *filename)
{
    ini_write(config, filename);
    ini_close(config);
    hdd_init();
    config = ini_read(filename);
    if (!config)
        fatal("Cannot reload test configuration: %s\n", filename);
    load_hard_disks();
}

static void
round_trip(const char *filename, int a, int b, int first_unit)
{
    config = ini_new();
    hdd_init();
    set_disk(a, HDD_BUS_SASI, first_unit);
    set_disk(b, HDD_BUS_SASI, first_unit ^ 1);
    for (int pass = 0; pass < 2; ++pass) {
        save_hard_disks();
        reload(filename);
        CHECK(hdd[a].bus_type == HDD_BUS_SASI);
        CHECK(hdd[b].bus_type == HDD_BUS_SASI);
        CHECK(hdd[a].sasi_channel == first_unit);
        CHECK(hdd[b].sasi_channel == (first_unit ^ 1));
        CHECK(hdd_is_valid(a) && hdd_is_valid(b));
        CHECK(strstr(hdd[a].fn, "disk") && strstr(hdd[b].fn, "disk"));
    }
    ini_close(config);
}

static void
legacy_and_cleanup(const char *filename)
{
    config = ini_new();
    hdd_init();
    set_disk(0, HDD_BUS_SASI, 0);
    set_disk(1, HDD_BUS_SASI, 1);
    save_hard_disks();
    ini_section_t cat = ini_find_section(config, "Hard disks");
    ini_section_delete_var(cat, "hdd_01_sasi_channel");
    ini_section_delete_var(cat, "hdd_02_sasi_channel");
    reload(filename);
    CHECK(hdd[0].sasi_channel == 0 && hdd[1].sasi_channel == 1);

    /* Saving a changed bus or removed disk must remove stale SASI keys. */
    save_hard_disks();
    set_disk(0, HDD_BUS_MFM, 1);
    hdd[1].fn[0] = '\0';
    save_hard_disks();
    reload(filename);
    cat = ini_find_section(config, "Hard disks");
    CHECK(!ini_has_entry(cat, "hdd_01_sasi_channel"));
    CHECK(!ini_has_entry(cat, "hdd_02_sasi_channel"));
    CHECK(hdd[0].bus_type == HDD_BUS_MFM && hdd[0].mfm_channel == 1);
    CHECK(!hdd_is_valid(1));

    /* Loading malformed or externally edited configurations cleans them too. */
    ini_section_set_int(cat, "hdd_01_sasi_channel", 1);
    ini_section_set_string(cat, "hdd_02_parameters", "17, 4, 615, 0, sasi");
    ini_section_set_int(cat, "hdd_02_sasi_channel", 1);
    reload(filename);
    cat = ini_find_section(config, "Hard disks");
    CHECK(!ini_has_entry(cat, "hdd_01_sasi_channel"));
    CHECK(!ini_has_entry(cat, "hdd_02_sasi_channel"));
    CHECK(!ini_has_entry(cat, "hdd_02_parameters"));
    ini_close(config);
}

int
main(int argc, char **argv)
{
    if (argc != 2)
        fatal("Usage: %s <temporary-config-file>\n", argv[0]);
    round_trip(argv[1], 0, 1, 0);
    round_trip(argv[1], 0, 1, 1);
    round_trip(argv[1], 2, 6, 1);
    legacy_and_cleanup(argv[1]);
    remove(argv[1]);
    printf("SASI configuration persistence: %u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
